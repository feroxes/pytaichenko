# Pytaichenko v2 wiring

Every wire is soldered once, at the length in the table below. The lengths come from the case model:
`python tools/wiring.py` prints the table (`--detail` adds the path lengths behind each one), and
`tests/test_wiring.py` checks that this file still matches it.

Directions: "front" is the side with the microphones and the antenna, "back" the side with the USB-C port.
Left and right are as seen from behind, with the USB-C port toward you.

## Parts

- ESP32-S3-DevKitC N16R8, headers soldered long side up.
- MAX98357A board with its 7-pin header and screw terminal, 1000 µF 16 V capacitor.
- INMP441 × 2 (without header pins).
- IRLZ44N transistor, 47 Ω resistor × 2, 6.2 kΩ resistor × 1.
- One LED filament (Ø1.5, 3 V), 12 × 12 button, Visaton FRS 5 X speaker.
- Thin stranded wire (26–30 AWG, silicone is easiest to tuck in) in red, brown, black, yellow, green, blue, white,
  grey and purple. Every wire to 3V3 or 5Vin is red and every ground wire is brown; black is only the speaker's
  "−", which is not ground (the amplifier drives both speaker wires).

## DevKitC pins

The board lies face up in the tray (labels readable), antenna to the front, USB-C ports at the back. Row 1 is the row
on the COM port side (right), row 2 the row on the second USB port side (left). Pins are counted from the antenna
end, 1 to 22. Every wire is soldered to the tip of a long pin, 4 mm above the board.

**IN-OUT jumper.** On this board USB power reaches the 5Vin pin only when the two small pads labelled `IN-OUT` are
joined; they sit by pins GPIO11 and GPIO12 of row 2, next to three diodes. Without the jumper the board starts, but 5Vin
stays at about 1 V: the ring stays dark and the amp is fed only through its signal pins, so the sound breaks up and
crackles (found on the first build). The pads are too far apart for a blob of solder, so join them with a short piece
of wire or a cut-off resistor lead. Leave the `USB-OTG` pair alone. With the pads joined, never feed 5Vin from another
supply while USB is plugged in. Check: with USB plugged in, 5Vin to GND reads 4.7–5 V.

| Row | Pin | Label | What it does | Wires |
|---|---|---|---|---|
| 2 | 1 | 3V3 | power for mic L | M1 |
| 2 | 2 | 3V3 | power for mic R | M6 |
| 2 | 5 | GPIO5 | mic clock (SCK) | M3, M8 |
| 2 | 6 | GPIO6 | mic word select (WS) | M4, M9 |
| 2 | 7 | GPIO7 | mic data (SD) | M5, M10 |
| 2 | 8 | GPIO15 | amp clock (BCLK) | A2 |
| 2 | 9 | GPIO16 | amp word select (LRC) | A1 |
| 2 | 10 | GPIO17 | amp data (DIN) | A3 |
| 2 | 11 | GPIO18 | ring (transistor gate, through 47 Ω) | R1 |
| 2 | 12 | GPIO8 | top button (pressed = connected to GND) | B1 |
| 2 | 21 | 5Vin | 5 V for the amp and the filament | A5, R4 |
| 2 | 22 | GND | ground for the ring and the button | R2, B2 |
| 1 | 1 | GND | ground for both mics | M2, M7 |
| 1 | 21 | GND | ground for the amp | A4 |

The GPIO numbers are the ones in the firmware (`v2/firmware/components/pt_audio/pt_audio.c` and
`pt_ui/pt_ui.c`). The button pin uses the chip's internal pull-up, so the button needs only two wires: GPIO8 and GND.
The board's own BOOT button (GPIO0) needs nothing.

Row 1 was read as `GND TX RX 1 2 42 41 40 39 38 37 36 35 0 45 48 47 21 20 GND GND` with the 22nd label hidden
under the USB port; the Espressif DevKitC-1 has `… 21 20 19 GND GND` there. Pin 21 is GND in both readings, so it is
the one used. Before soldering, check with a multimeter that row 1 pin 21 beeps to row 2 pin 22 (GND).

The mic power and ground wires go to their own pins, away from the amp and ring wires (the hum in version 1 most
likely came through shared power and ground).

## Wires

Each mic is wired straight to the DevKitC, not from one mic to the other, so GPIO5, 6 and 7 and row 1 pin 1 each take
two wires. Twist the two bared ends together before soldering them to the pin.

| # | From | To | Length | Colour | Joins |
|---|---|---|---|---|---|
| M1 | mic L VDD | DevKitC 3V3 (row 2, pin 1 from the antenna) | 4.5 cm | red | tray ↔ body |
| M2 | mic L GND | DevKitC GND (row 1, pin 1 from the antenna) | 7.0 cm | brown | tray ↔ body |
| M3 | mic L SCK | DevKitC GPIO5 (row 2, pin 5 from the antenna) | 5.0 cm | yellow | tray ↔ body |
| M4 | mic L WS | DevKitC GPIO6 (row 2, pin 6 from the antenna) | 5.0 cm | green | tray ↔ body |
| M5 | mic L SD | DevKitC GPIO7 (row 2, pin 7 from the antenna) | 5.0 cm | blue | tray ↔ body |
| M6 | mic R VDD | DevKitC 3V3 (row 2, pin 2 from the antenna) | 8.5 cm | red | tray ↔ body |
| M7 | mic R GND | DevKitC GND (row 1, pin 1 from the antenna) | 9.5 cm | brown | tray ↔ body |
| M8 | mic R SCK | DevKitC GPIO5 (row 2, pin 5 from the antenna) | 9.0 cm | yellow | tray ↔ body |
| M9 | mic R WS | DevKitC GPIO6 (row 2, pin 6 from the antenna) | 9.5 cm | green | tray ↔ body |
| M10 | mic R SD | DevKitC GPIO7 (row 2, pin 7 from the antenna) | 9.5 cm | blue | tray ↔ body |
| A1 | amp LRC | DevKitC GPIO16 (row 2, pin 9 from the antenna) | 5.0 cm | green | tray |
| A2 | amp BCLK | DevKitC GPIO15 (row 2, pin 8 from the antenna) | 5.5 cm | yellow | tray |
| A3 | amp DIN | DevKitC GPIO17 (row 2, pin 10 from the antenna) | 6.0 cm | blue | tray |
| A4 | amp GND | DevKitC GND (row 1, pin 21 from the antenna) | 5.5 cm | brown | tray |
| A5 | amp Vin | DevKitC 5Vin (row 2, pin 21 from the antenna) | 8.0 cm | red | tray |
| S1 | speaker + | amp terminal + | 17.5 cm | red | tray ↔ lid |
| S2 | speaker − | amp terminal − | 17.5 cm | black | tray ↔ lid |
| R1 | transistor G (47 Ω soldered on it) | DevKitC GPIO18 (row 2, pin 11 from the antenna) | 4.5 cm | white | tray |
| R2 | transistor S | DevKitC GND (row 2, pin 22 from the antenna) | 9.0 cm | brown | tray |
| R3 | filament − (right end) | transistor D | 14.0 cm | grey | tray ↔ body |
| R4 | filament + (left end, 47 Ω soldered on it) | DevKitC 5Vin (row 2, pin 21 from the antenna) | 6.0 cm | red | tray ↔ body |
| B1 | button leg A | DevKitC GPIO8 (row 2, pin 12 from the antenna) | 14.0 cm | purple | tray ↔ lid |
| B2 | button leg B | DevKitC GND (row 2, pin 22 from the antenna) | 15.0 cm | brown | tray ↔ lid |

The length is the piece of wire to cut, bared ends included (about 3 mm bared at each end). For R1 and R4 the
47 Ω resistor makes up the rest of the run.

## The circuits

- **Microphones.** Both INMP441 share the clock (SCK), word select (WS) and data (SD) lines; the L/R pad decides
  which half of each frame a mic answers in. Mic L (the left one) gets L/R joined to its own GND with a short piece
  of wire on the chip side; mic R gets L/R joined to its own VDD. The firmware reads both: the left channel is mic L,
  the right channel mic R, and its log line `raw microphones: left …, right …` follows the same names. If the sound
  front end does not start, the firmware falls back to the left channel alone, so mic L is the one that keeps
  working. Keep the side with the sound hole flat; bridge L/R on the chip side only.
- **Amplifier.** Header pins in the order LRC, BCLK, DIN, GAIN, SD, GND, Vin, with Vin at the end next to the
  capacitor leads (check against the print on your board). The capacitor's long leg (+) goes to Vin and the short leg
  (−, striped side) to GND, low on the pins so the tips stay free for A4 and A5. GAIN and SD stay unconnected, as in
  version 1: GAIN open gives the middle gain (9 dB), and SD open leaves the amp on, playing the average of left and
  right; the firmware sends the same sound in both, so nothing is lost.
- **Ring.** One filament: 5Vin → 47 Ω → filament "+"; filament "−" → transistor D (middle leg); transistor S →
  GND; GPIO18 → 47 Ω → transistor G; 6.2 kΩ between G and S, so the gate is held off while the board starts.
  The filament's 47 Ω is soldered straight onto its "+" contact, in line with the tail, and covered together with
  the joint by heat-shrink or tape; R4 runs from its free end to 5Vin, where it shares the pin tip with A5. In line
  with the tail the resistor (a 1/4 W body, about 6.5 × Ø2.5, Ø3.5 with heat-shrink) clears the body, the lid
  column, the lid and the speaker by more than 1 mm. The
  transistor lies on the tray floor with its metal tab down and its legs to the front: then G is the leg nearest the
  DevKitC and S the one farthest from it. The metal tab is D, so it must not touch any other joint. Keep the bare
  leads of both 47 Ω resistors short and clear of the other pins and joints.
- **Speaker.** Amp terminal + to the speaker lug marked "+", − to the lug marked "−" (the marks are printed by
  the lugs). Polarity only matters for consistency; the speaker works either way.

## Soldering order

Everything is soldered on the bench first, the DevKitC wires included; the parts then go into the case with their
wires on, in the assembly order of `README.md`. Tip: snap the DevKitC and the amp into the tray and glue the
transistor in before soldering at the DevKitC (steps 1–3 of the assembly), so the tray holds them still.

1. On the bench, part by part:
   - mics: the L/R jumper first (mic L to GND, mic R to VDD), then the five wires on the chip side (M1–M5 on mic L,
     M6–M10 on mic R). Tie each bundle with a tag "L" or "R";
   - amp: header and terminal (if not done), the capacitor, then A1–A5 on the pin tips;
   - filament: find "+" with a CR2032 coin cell (it lights only one way round) and mark it. Solder R3 (grey) to the
     "−" contact, and a 47 Ω resistor to the "+" contact in line with it (short lead), then R4 (red) to the free
     end of that resistor. Solder on the 5 mm metal contact only and quickly. Cover the resistor and the joint with
     heat-shrink or tape: it sits by the wall inside the body;
   - transistor: 6.2 kΩ between G and S, 47 Ω on G, R1 (white) to the free end of that 47 Ω, R2 (brown) to S, and
     R3 from the filament to D;
   - button: find the pair of legs that closes when pressed, cut the other two short, then B1 (purple) and B2
     (brown);
   - speaker: S1 (red) to the lug marked "+", S2 (black) to the lug marked "−".
2. At the DevKitC, still on the bench: join the `IN-OUT` pads (see DevKitC pins). Then check with a multimeter that row 1 pin 21 beeps to row 2 pin 22 (GND).
   If it does not, leave row 1 pin 21 alone, cut A4 to 8.0 cm instead and solder it to row 2 pin 22 as a third wire.
   Then, in this order: A1–A4, A5 and R4 twisted together on 5Vin, R1, R2, then M1–M10, B1 and B2. Last, screw S1
   and S2 into the amp terminal (this can wait until the lid stage). Lead each wire sideways off the pin tip right away:
   in the case there are only 7 mm between the pin tips and the bottom of the speaker magnet.
3. Checks with the USB unplugged: 5Vin to GND silent (a short beep at first is the capacitor charging), 3V3 to GND
   silent.
4. Tray: DevKitC, amp (slide it under the lip and press until the latch clicks) and transistor (hot glue, tab down,
   legs to the front) if they are not in yet, then the tray gasket in its groove, before any wire crosses the rim.
   The checks of step 3 again.
5. Body: inserts first, then glue the mics in (mic L in the left hole), lay the filament in the ring channel with
   its “+” end on the left, then the ring arc. The wires hang out of the open bottom.
6. Body onto the tray: pass the speaker and the button with their wires up through the body and out of its open top.
   Lay the body on its side next to the tray's front-left edge (the rim by mic L, 150° counted from the right toward
   the front), its open bottom facing the tray about 1 cm away, raised on something about 2 cm high so its lowest
   side sits just above the tray rim. This is the tray swung open 90° about that edge; every length in the table
   covers it. Tip the body over onto the tray along that edge, keeping the extra wire along the wall and off the
   gasket as it comes down, then turn the body and tray over, holding the speaker and the button, and drive the tray
   screws from below. Feet and bushing.
7. Lid last: speaker and button in their places in the lid, their wires running down into the body. S1 and S2 into
   the amp terminal if not done yet. A first power-up over USB at this point, before closing, shows the ring and the
   sound while everything is still reachable. Then the lid onto the body, lid screws, plugs. Above the DevKitC pin
   tips there are only 7 mm to the bottom of the speaker magnet, so keep the wires along the wall, not bunched over
   the boards.

## How the lengths are worked out

`tools/wiring.py` reads every position from the model (`part="params"`) and the GPIO numbers from the firmware.

- Ends: DevKitC and amp pin tips, the amp terminal openings (5.0 pitch, 3.5 above the board, facing the front), the
  transistor leg tips, the chip side of each mic at its centre (the pads are within ±4 mm of it), the button's
  inner legs, the speaker lugs (± a quarter of the terminal strip width from the centre) and the end of each filament
  contact.
- Closed route: from each tray end up to a wire level 2 mm above the amp (z 15.9, 3.2 under the magnet), straight
  across at that level, then up to the other end. Wall-side ends (mics, filament) first move in to 32.8 from the
  centre, inside the tray screw bosses, then run straight down; the button and speaker wires follow the channels
  at the back of the body (`wire_btn_pts`, `wire_term_pts`). A test checks that none of these paths runs through the
  case or a part.
- Service length: a wire between the tray and the body or the lid must also reach in the soldering pose, the tray
  swung 90° about a hinge 5 mm outside the rim at the seam (10 mm between the rims), and at every angle on the way
  back to closed. The hinge side and the filament's "+" side are the ones with the least wire in total: 150°
  (front-left) and the left end.
- Each run is the longer of the closed and the opened path, × 1.1 for bends, plus 5 mm at each end for baring and
  soldering, minus 12 mm where a 47 Ω resistor is in the run, rounded up to 0.5 cm.
- The microphone wires are then cut 2 cm shorter: on the first build the computed lengths left too much spare
  there (`MIC_FIT_TRIM`). The filament wires were measured on the same build: R3 is 2 cm and R4 5.5 cm shorter
  than computed (`FIL_MINUS_FIT_TRIM`, `FIL_PLUS_FIT_TRIM`).
