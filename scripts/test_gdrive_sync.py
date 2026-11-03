#!/usr/bin/env python3
"""Offline unit tests for gdrive_sync.download_file (timeout + retry/resume).

Run: python3 scripts/test_gdrive_sync.py

No network and no credentials are used: googleapiclient/httplib2 are replaced
with fakes in sys.modules and the Drive service is a stub, so a simulated
next_chunk() timeout exercises the real retry/cleanup logic in gdrive_sync.
"""

import contextlib
import io
import socket
import sys
import tempfile
import types
import unittest
import unittest.mock
from pathlib import Path

SCRIPTS_DIR = Path(__file__).resolve().parent
sys.path.insert(0, str(SCRIPTS_DIR))

import gdrive_sync  # noqa: E402

ServerNotFoundError = type("ServerNotFoundError", (Exception,), {})
HttpLib2Error = type("HttpLib2Error", (Exception,), {})

REQUESTS_CREATED = []


class FakeProgress:
    def __init__(self, ratio):
        self._ratio = ratio

    def progress(self):
        return self._ratio


class FakeHttp:
    def __init__(self):
        REQUESTS_CREATED.append(self)
        self.timeout = None


class FakeRequest:
    def __init__(self):
        self.http = FakeHttp()


class FakeFiles:
    def get_media(self, fileId):
        return FakeRequest()


class FakeService:
    def files(self):
        return FakeFiles()


class FakeDownloader:
    """Mimics MediaIoBaseDownload: Range offset advances only on success."""

    def __init__(self, stubber, fd, request, chunksize):
        stubber.chunksize = chunksize
        stubber.fd = fd
        self._stubber = stubber

    def next_chunk(self):
        stub = self._stubber
        if stub.call_index >= len(stub.scheduler):
            raise AssertionError("more next_chunk calls than scripted")
        outcome = stub.scheduler[stub.call_index]
        stub.call_index += 1
        if isinstance(outcome, Exception):
            stub.fd.write(f"partial-after-{stub.call_index}\n".encode())
            raise outcome
        stub.fd.write(b"chunk-body\n")
        return FakeProgress(outcome), True


class Stubber:
    """Installs/removes fake googleapiclient + httplib2 modules around a test."""

    def __init__(self, scheduler):
        self.scheduler = scheduler
        self.call_index = 0
        self.chunksize = None
        self.fd = None
        self._saved = None

    def install(self):
        self._saved = {
            name: sys.modules[name]
            for name in ("googleapiclient", "googleapiclient.http", "httplib2")
            if name in sys.modules
        }
        root = types.ModuleType("googleapiclient")
        ghttp = types.ModuleType("googleapiclient.http")
        ghttp.MediaIoBaseDownload = lambda fd, request, chunksize=1: FakeDownloader(
            self, fd, request, chunksize
        )
        root.http = ghttp
        h2 = types.ModuleType("httplib2")
        h2.ServerNotFoundError = ServerNotFoundError
        h2.HttpLib2Error = HttpLib2Error
        sys.modules["googleapiclient"] = root
        sys.modules["googleapiclient.http"] = ghttp
        sys.modules["httplib2"] = h2

    def restore(self):
        for name in ("googleapiclient", "googleapiclient.http", "httplib2"):
            sys.modules.pop(name, None)
        sys.modules.update(self._saved or {})


class DownloadRetryTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix="gdrive_sync_test_")
        self.addCleanup(self.tmp.cleanup)
        self.dest = Path(self.tmp.name) / "IMG_1234.jpg"

    def run_download(self, scheduler):
        stubber = Stubber(scheduler)
        stubber.install()
        self.addCleanup(stubber.restore)
        sleep = unittest.mock.MagicMock()
        out = io.StringIO()
        with contextlib.redirect_stdout(out), unittest.mock.patch.object(
            gdrive_sync.time, "sleep", sleep
        ):
            try:
                gdrive_sync.download_file(
                    FakeService(), "file-1234", self.dest, label="IMG_1234.jpg"
                )
                error = None
            except Exception as exc:
                error = exc
        return stubber, sleep, error, out.getvalue()

    def test_timeout_then_retry_succeeds_and_resumes(self):
        timeout = TimeoutError("The read operation timed out")
        stubber, sleep, error, output = self.run_download([timeout, 1.0])

        self.assertIsNone(error)
        self.assertEqual(stubber.call_index, 2)
        self.assertTrue(self.dest.exists())
        self.assertEqual(self.dest.read_bytes(), b"partial-after-1\nchunk-body\n")
        self.assertEqual(stubber.chunksize, gdrive_sync.DOWNLOAD_CHUNK_SIZE)
        self.assertNotIn("Download of IMG_1234.jpg failed", output)

    def test_retry_and_progress_visible_in_output(self):
        output = self.run_download([TimeoutError("The read operation timed out"), 1.0])[3]
        self.assertIn(
            "Retrying chunk for IMG_1234.jpg (attempt 2/3) after TimeoutError: "
            "The read operation timed out",
            output,
        )
        self.assertIn("Downloaded 100%", output)

    def test_http_timeout_is_set_on_media_request(self):
        REQUESTS_CREATED.clear()
        _, _, error, _ = self.run_download([1.0])

        self.assertIsNone(error)
        self.assertEqual(len(REQUESTS_CREATED), 1)
        self.assertEqual(
            REQUESTS_CREATED[0].timeout, gdrive_sync.DOWNLOAD_TIMEOUT_SECS
        )

    def test_three_consecutive_timeouts_delete_partial_and_raise(self):
        timeout = TimeoutError("The read operation timed out")
        stubber, sleep, error, output = self.run_download(
            [timeout, timeout, timeout, 1.0]  # 4th entry never consumed
        )

        self.assertIsInstance(error, TimeoutError)
        self.assertEqual(stubber.call_index, 3)
        self.assertFalse(self.dest.exists())
        self.assertIn(
            "Download of IMG_1234.jpg failed after 3 consecutive TimeoutErrors",
            output,
        )
        backoffs = [call.args[0] for call in sleep.call_args_list]
        self.assertEqual(backoffs, [5, 10])

    def test_other_transient_error_families_are_retried(self):
        scheduler = [
            socket.timeout("read timed out"),
            ServerNotFoundError("DNS broke"),
            1.0,
        ]
        stubber, _, error, output = self.run_download(scheduler)
        self.assertIsNone(error)
        self.assertEqual(stubber.call_index, 3)
        self.assertTrue(self.dest.exists())
        self.assertEqual(output.count("Retrying chunk for IMG_1234.jpg"), 2)


if __name__ == "__main__":
    unittest.main()