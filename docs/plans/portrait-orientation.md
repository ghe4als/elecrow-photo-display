# Plan: Portrait Orientation with Power Plug at Bottom

## Goal
Mount the CrowPanel 5" HMI V3.0 in portrait orientation (power plug at the bottom) and have photos display correctly. Portrait phone photos fill the screen; landscape photos are rotated 90° to fill the portrait frame.

## Background
The panel native resolution is 800×480 landscape. Previous attempts to rotate via LovyanGFX `offset_rotation=4` + `setRotation(2)` or LVGL `lv_display_set_rotation` produced a blank/corrupted screen and were reverted. We need a safer approach.

## Proposed approach
1. **Hardware-level panel rotation via LovyanGFX config.**
   - Change `panel_width` / `panel_height` in the LovyanGFX setup to swap the active area to 480×800.
   - Set `offset_rotation` to the value that puts the power connector at the bottom without breaking RGB full-framebuffer flush.
   - Keep LVGL display size matching the rotated panel (480×800).

2. **LVGL display rotation as fallback.**
   - If LovyanGFX rotation still blanks the screen, use LVGL `lv_display_set_rotation` on a 800×480 display and render into a rotated framebuffer.
   - This is less efficient but was partially attempted before; we will isolate variables.

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
