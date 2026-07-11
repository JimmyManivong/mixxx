# FLX6 controller mapping backup

Live copy of the DDJ-FLX6 mapping running on the Pi, backed up here because
Mixxx reads controller mappings from `~/.mixxx/controllers/` (a per-user
config directory, not `res/`), so nothing here was version-controlled until
now. If the Pi's SD card is ever reflashed or a fresh Mixxx install is set
up, restore by copying both files into `~/.mixxx/controllers/`.

Customizations on top of the community mapping (see project memory
`project-flx6-live-debugging` for the full story):

- BEAT FX 8-seg LED readout (0xB4/0xB5 CC) driven from the script, stepping
  through musical beat divisions instead of the stock effect-selector
  behavior the BEAT arrows used to trigger.
- VU-meter LEDs scaled by the channel volume fader (`vuMeterUpdate`).
- `focusedFxGroup()` honors `PioneerDDJFLX6.fxSelect` so FX2's controls
  (LEVEL/DEPTH, ON/OFF, CH-assign, BEAT) act on FX2 when selected, not
  always FX1.

Restore command:

```bash
scp pi-deploy/controllers/Pioneer-DDJ-FLX6.midi.xml \
    pi-deploy/controllers/Pioneer-DDJ-FLX6-script.js \
    jimmy@<pi-ip>:~/.mixxx/controllers/
```
