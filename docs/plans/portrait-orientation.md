# Plan: Portrait Orientation with Power Plug at Bottom

## Goal
Mount the CrowPanel 5" HMI V3.0 in portrait orientation (power plug at the bottom) and have photos display correctly. Portrait phone photos fill the screen; landscape photos are rotated 90° to fill the portrait frame.

## Background
The panel native resolution is 800×480 landscape. Previous attempts to rotate via LovyanGFX `offset_rotation=4` + `setRotation(2)` or LVGL `lv_display_set_rotation` produced a blank/corrupted screen and were reverted. We need a safer approach.

## Results
- **Attempt 1 (LovyanGFX `offset_rotation=1`):** Reverted. Flashing the firmware produced a screen full of rainbow/vertical color lines instead of an image. The RGB panel driver does not tolerate `offset_rotation` with the full-framebuffer flush path used on this board.
- **Reversion:** The code was reverted to the known-good landscape configuration and the device was reflashed successfully. It is back to the working state.

## Next options to try
1. **LVGL software rotation on a landscape display.**
   - Keep LovyanGFX at 800×480 and set `lv_display_set_rotation(LV_DISPLAY_ROTATION_90)`.
   - LVGL then renders into a rotated framebuffer. The flush callback must account for the rotated buffer layout or rely on LVGL's internal rotation.
   - Risk: previous attempt with `lv_display_set_rotation` also blanked the screen, but it may have been combined with other changes.

2. **Software rotate each photo at decode time.**
   - Keep the panel in landscape. For each portrait source, use LVGL `lv_image_set_rotation(900)` to show it upright.
   - This does not rotate the whole UI (counters, touch mapping), so it only solves the photo orientation, not the physical mounting orientation.

3. **Pre-rotate all photos on the host.**
   - Keep the panel in landscape. Rotate every portrait photo 90° in `prepare_photos.py` and pad it to 800×480 landscape.
   - The frame then displays pre-rotated images. Touch and UI stay in landscape.
   - This is the simplest and safest path if the goal is just to show portrait photos upright on a landscape-mounted panel.

3. **Touch coordinate rotation.**
   - The GT911 touch controller reports coordinates in the panel's native orientation.
   - Add a transformation in the LVGL input device callback: swap X/Y and mirror the appropriate axis depending on which rotation angle is selected.

4. **Photo auto-rotation.**
   - In `load_photo()`, detect whether the source image is landscape (width > height) or portrait.
   - If landscape and the display is portrait, rotate the image 90° via `lv_image_set_rotation()` so it fills the 480×800 screen.
   - Update scaling math to use the rotated dimensions.

## Files to edit
- `src/crowpanel/main.cpp` — LovyanGFX panel dimensions/rotation, LVGL display size.
- `src/crowpanel/touch.cpp` — GT911 coordinate transform for the selected rotation.
- `src/crowpanel/photo_viewer.cpp` — image scaling/rotation logic for portrait display.
- `include/crowpanel/photo_viewer.h` — expose display orientation flag if needed.

## Verification
1. Build `crowpanel-5` and flash.
2. Confirm boot logo/photo appears in portrait orientation without blank screen.
3. Confirm touch: tap pauses, swipe up/down advances (or left/right depending on mapping), long press tests backlight.
4. Confirm a portrait phone photo fills the 480×800 screen.
5. Confirm a landscape photo is rotated and fills the portrait screen.

## Out of scope
- Cloud sync changes (already complete).
- Additional transition effects.
