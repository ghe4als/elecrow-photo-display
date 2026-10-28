# Phase 2: Touch Navigation

## Goal
Let the user control the SD-card slideshow with touch: tap to pause/resume, swipe left/right to change photos.

## Scope
- Wire `crowpanel::touch.cpp` (GT911 via PCA9557) into `photo_viewer.cpp` as an LVGL indev event listener.
- Implement gestures in `photo_viewer.cpp`:
  - short tap anywhere → pause/resume auto-advance;
  - swipe left → next photo;
  - swipe right → previous photo.
- Provide an on-screen pause indicator (e.g. a small overlay icon or label) that appears while paused.
- Keep auto-advance at 10 s when not paused.
- Continue to work offline (no cloud).

## Files to add
- `include/crowpanel/touch.h` if not already present; otherwise extend it with a public gesture API.
- No new source files expected unless a small gesture helper is extracted.

## Files to edit
- `include/crowpanel/photo_viewer.h` — add `photo_viewer_handle_event(lv_event_t*)` and pause-state helpers.
- `src/crowpanel/photo_viewer.cpp` — implement gesture detection and the pause overlay.
- `src/crowpanel/main.cpp` — register the photo-viewer screen (or its image object) to receive indev events.

## Verification
1. `pio run -e crowpanel-5` exits 0.
2. `pio test -e native` passes.
3. Serial log shows tap/swipe events and pause/resume state changes.
4. Display advances on swipe and pauses on tap.

## Out of scope for Phase 2
- Cross-fade or animated transitions between photos.
- Thumbnail/browser mode.
- Google Drive sync.
