# Pytaichenko v2 case

OpenSCAD model of the Ø84 case. Every number is in `params.scad`; `bash build.sh` writes the files in `stl/`.
Tests: `python -m pytest -q tests` (OpenSCAD, trimesh, manifold3d).

Size: Ø84 × 60.4, 62.4 with the feet.

## What to print

| Part | File | Plastic | Count |
|---|---|---|---|
| lid (sunflower grille, button holder, speaker standoffs) | `stl/PETG-black_top.stl` | PETG black | 1 |
| body | `stl/PETG-black_body.stl` | PETG black | 1 |
| tray (bottom with the DevKit and the amp) | `stl/PETG-black_tray.stl` | PETG black | 1 |
| button cap | `stl/PETG-black_button_cap.stl` | PETG black | 1 |
| ring arc | `stl/PETG-white_ring_arc.stl` | PETG white | 1 |
| speaker gasket | `stl/TPU_speaker_gasket.stl` | TPU | 1 |
| lid gasket | `stl/TPU_top_gasket.stl` | TPU | 1 |
| tray gasket | `stl/TPU_tray_gasket.stl` | TPU | 1 |
| plug over a lid screw | `stl/PETG-black_top_plug_x3.stl` | PETG black | 3 |
| foot | `stl/TPU_foot_x4.stl` | TPU | 4 |
| USB-C bushing (closes the cable opening) | `stl/TPU_com_bushing.stl` | TPU | 1 |

Each file holds one part already lying the right way on the bed; `_xN` is the number of copies.

## Lid face

The top face is a golden-angle spiral, like the seeds of a sunflower: 46 through holes Ø3.8 over the speaker, then
82 dimples 0.8 deep, Ø3.4–2.4, out to the edge. The lid prints face down without supports; the bottom of each
dimple is a short bridge. `bash build.sh` also writes a top view (`png/top_face.png`) and a view of the closed case
(`png/assembly_closed.png`).

The bushing is pressed into the USB-C opening of the tray from outside and closes the gap around the cable plug.
Its hole fits a plug overmold of 10 × 6.2 (`com_plug_w`, `com_plug_h` in `params.scad`, 0.1 clearance per side).
For another cable, measure the overmold, change those two values and rerun `build.sh`. A plug wider than 11.4
or taller than 6.4 leaves the bushing walls too thin: print without the bushing (the opening takes a 12 × 7
overmold).

## How to print

Bambu Lab X1 Carbon, 0.4 nozzle, first layer 0.2, Arachne wall generator (the stock profile uses Classic).

| Plastic | Settings |
|---|---|
| PETG black | layer 0.16, outer wall 60 mm/s, inner wall 100 mm/s at 5000 mm/s², 4–5 walls, Arachne, no supports |
| PETG white | layer 0.16, at least 2 walls, Arachne, no supports |
| TPU | layer 0.16, infill 100 % (zig-zag), Arachne, no supports |

- Print the body and the ring arc at 0.16. The arc is 2.88 tall (`ring_arc_h`), the height of the body bridge
  printed at 0.16; the arc itself prints 0.04 taller, so the lid presses it lightly and it does not rattle. At
  another layer height, change `body_layer` and rerun `build.sh`.
- The lid prints face down, the button cap top down, the arc on its flat top face, the plugs face down.
- Seam: "back". The body and the tray load with their front (the mic and antenna side) toward the back of the
  bed, so turn both 180° on the plate before slicing; the seam then lands at the back of the speaker, on the button
  and USB-C side. The lid already loads the right way round.
- No supports anywhere. The short bridges are expected: the USB-C opening of the tray, the foot niches, the plug
  seat ledges and countersinks of the lid. Turn on "Thick bridges" (Quality → Bridging): a bridge one layer thin
  sags into strings. Dry the PETG first (65 °C, 6–8 h); wet PETG strings everywhere.

## Hardware

- M2.5 heat-set inserts, 4 long, Ø3.5: 9 (2 in the lid standoffs, 3 in the body columns, 4 in the body from below).
- M2.5 × 8 countersunk Phillips screws: 9 (2 speaker, 3 lid, 4 tray).
- M2.5 DIN 125 washers (2.7 / 6 / 0.5): 2, under the speaker ears.

Order: tray first (DevKitC, amp, transistor, tray gasket), then the body onto the tray (screws from below, feet,
bushing into the USB-C opening), the lid last (speaker screwed in from below, then the lid onto the body with screws
from above and the plugs).

Every wire, the DevKit pins included, is soldered on the bench before assembly. Cut every wire to the length in
[WIRING.md](WIRING.md): those lengths already include the tray swung open beside the body, so add nothing to them.
Pin map, wire lengths and soldering order: [WIRING.md](WIRING.md).

## Assembly steps

26 steps in three stages: Tray 1–4, Body onto the tray 5–15, Lid 16–26. `bash render_steps.sh` draws
each step from `steps.scad` into `site/assets/renders/v2/assembly-NN.png` (the part added in that step in orange,
parts already in place in grey) and the closed speaker from `hero.scad` into `site/assets/renders/v2/hero.png`.
Wires are not drawn; they are all soldered on the bench before assembly, as in [WIRING.md](WIRING.md), so every part
goes into the case with its wires on.

| Step | Stage | Caption |
|---|---|---|
| 1 | Tray | If the DevKitC is not in the tray yet, press it into the rails, antenna to the front, until the latches click. |
| 2 | Tray | The same for the amp if it is not in place yet: tilt it, slide its edge under the lip on the DevKitC rail and press the far edge down until the latch clicks. To take it out, push the latch towards the wall with a thin screwdriver. |
| 3 | Tray | If the transistor is not glued yet, glue it to the floor, tab down and legs to the front. |
| 4 | Tray | Lay the tray gasket in its groove (it sits loose, 0.2 clearance on each side) and repeat the multimeter checks. |
| 5 | Body onto the tray | Turn the body upside down and melt four inserts into the bosses at its bottom edge. |
| 6 | Body onto the tray | Turn the body back up and melt three inserts into the tops of the lid columns. |
| 7 | Body onto the tray | Set the mic tagged L into the left seat, sound hole against the wall and chip side facing in. |
| 8 | Body onto the tray | Set the mic tagged R into the right seat the same way. |
| 9 | Body onto the tray | Glue each mic with liquid hot glue around the edge of its board only, away from the hole. |
| 10 | Body onto the tray | Lay the filament in the ring channel with its plus end on the left and pass both ends in through the slots at the back. |
| 11 | Body onto the tray | Press the ring arc into the channel over the filament. |
| 12 | Body onto the tray | Pass the speaker and the button up through the body, lay the body on its side by the front-left edge of the tray and tip it over onto the tray along that edge, keeping the extra wire along the wall and off the gasket. |
| 13 | Body onto the tray | Turn the body and tray over, holding the speaker and the button, and drive the four tray screws. |
| 14 | Body onto the tray | Press the four feet into their niches. |
| 15 | Body onto the tray | Press the bushing into the USB-C opening from outside; the cable plugs in through it. |
| 16 | Lid | Turn the lid face down and melt two inserts into the speaker standoffs. |
| 17 | Lid | Put the button cap into its hole from inside. |
| 18 | Lid | Press the switch into its holder until both hooks click over it. |
| 19 | Lid | Put a drop of glue by each hook and check by pressing hard on the cap. |
| 20 | Lid | Lay the speaker gasket on the lip around the grille. |
| 21 | Lid | Set the speaker on the gasket with its ears on the standoffs. |
| 22 | Lid | Put a washer on each screw and drive both screws through the speaker ears until the ears sit flat. |
| 23 | Lid | Lay the lid gasket in the groove around the rim (it sits loose, 0.2 clearance on each side). |
| 24 | Lid | Screw the speaker wires into the amp terminal if they are not in yet, power up over USB to check the ring and the sound if you like, then set the lid on the body with the wires tucked down inside. |
| 25 | Lid | Drive the three lid screws from above. |
| 26 | Lid | Press the three plugs into the screw seats, flush with the face. |

## First-print checks

- Tray: the web above the USB-C opening is only 0.55 thick (between the opening and the gasket groove).
- Tray: every latch has a 0.6 fillet at its root (1.0 on the two DevKitC latches by the antenna) and stands free of
  the rail and the nest walls on its whole height (0.4 slits, also between the rail ends and the antenna-end
  latches). If a slit printed fused, open it with a knife before fitting the board, or the latch bends only above
  the joint and snaps.
- Tray: the latches only hold the DevKitC down. When a USB-C cable is pushed in, the header strips stop against the
  closed front ends of their slots (0.1 play), while the antenna-end latches are 0.35 away from the board edge. When
  the cable is pulled out, the back corners of the board stop against the tray wall after about 0.2. If the board
  does not drop fully into the slots, check the slot ends for blobs before pressing harder.
- Tray: the amp latch is short, so it bends further than the DevKitC latches (strain 0.024). If it cracks, a drop of
  hot glue on that edge holds the amp just as well.
- Body: glue each microphone with liquid hot glue around the edge of its pad only. No glue under the board or near
  the hole.
- Lid: glue the button switch in its holder (a drop by each hook) and check by pressing hard on the cap.
- Button cap: it can lift 0.5 and has 0.2 side play, so it may rattle. Glue it to the switch pusher with a small
  drop.
- Lid: the ear standoff height comes from the measured speaker rim (1.3). With the ears screwed flat, the speaker
  gasket should be squeezed evenly and the ears not bent.
- Lid: the lid gasket groove has walls of 0.55 on each side; check they printed whole.
- Lid gasket: squeezed 25 % on the bridge, 15 % over the ring arc in the model (a little more with printed
  layers). Check the seam is closed over the arc.
- Plugs: PETG, 0.15 smaller than their seats all round, with 8 thin ribs that reach 0.02 into the seat wall; a printed hole comes out a little smaller, which gives the real grip. Each plug is 1.0 tall in a 1.2 deep seat, so a sagging seat ledge does not lift it. Press them in with something flat laid across the face, so they stop flush. To open the lid, lever a plug out with a knife tip in the small chamfer around its face. If the ribs printed too fat, sand them; if a plug falls out, a drop of glue on one rib.
- Bushing: 0.15 larger than the opening; it should press in by hand, flush with the outside, and the plug should
  click fully into the DevKit socket through it.
