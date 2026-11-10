include <voice-case.scad>

part = "params";
step = 1;

new_color = "darkorange";
old_color = "lightslategray";
wire_color = "gold";

arc_lift = 18;
top_lift = 24;
screw_lift = 16;
plug_lift = 30;
tray_drop = 46;
tray_screw_drop = 26;
insert_lift = 13;
mic_lift = 10;
part_lift = 14;

tr_x = -25;
tr_y = 0;

arc_h = 2.88;
arc_axis_z = arc_h / 2;
arc_end_deg = 90 - bridge_half_deg - 180 / PI * 0.1 / filament_r;

module old() color(old_color) children();
module new() color(new_color) children();

module flip(lift = 0) {
    translate([0, 0, H + lift]) rotate([180, 0, 0]) children();
}

module flip_tray(lift = 0) {
    translate([0, 0, lift]) rotate([180, 0, 0]) children();
}

module insert_part() {
    cylinder(d = insert_od, h = insert_len, $fn = 32);
}

module boss_inserts(dz = 0) {
    for (a = tray_screw_angles)
        rotate([0, 0, a]) translate([tray_screw_r, 0, tray_pad_t + dz]) insert_part();
}

module post_inserts(dz = 0) {
    for (a = top_screw_angles)
        at_polar(top_screw_r, a) translate([0, 0, top_col_z1 - insert_len + dz]) insert_part();
}

module top_inserts(dz = 0) {
    for (a = spk_hole_angles)
        at_polar(spk_hole_r, a) translate([0, 0, gasket_z1 + dz]) insert_part();
}

module tray_inserts(dz = 0) {
    for (sx = [-1, 1])
        translate([sx * dk_hole_x, dk_y0() + dk_hole_in, dk_z0 - insert_len + dz]) insert_part();
}

module mic_bead() {
    rotate([0, 0, mic_az]) translate([mic_face_r(), 0, mic_z]) rotate([0, 90, 0])
        translate([0, 0, -mic_t - 1.0]) difference() {
            cylinder(d = mic_d + 2.6, h = 1.0 + 0.2, $fn = 64);
            translate([0, 0, -1]) cylinder(d = mic_d - 1.4, h = 4, $fn = 64);
        }
}

module mic_moved(dx = 0) {
    rotate([0, 0, mic_az]) translate([-dx, 0, 0]) rotate([0, 0, -mic_az]) dummy_mic();
}

module wire_path(pts, d = 0.9) {
    for (i = [0 : len(pts) - 2]) hull() {
        translate(pts[i]) sphere(d = d, $fn = 12);
        translate(pts[i + 1]) sphere(d = d, $fn = 12);
    }
}

module fil_wires() {
    chan_at_bridges() {
        p0 = chan_pt(chan_len - 1.2);
        ci = chan_in(p0);
        p1 = p0 + 2.5 * ci;
        p2 = p1 + 6 * ci + [0, 0, -7];
        p3 = p2 + 6 * ci + [0, 0, -22];
        wire_path([p0, p1, p2, p3], 0.9);
    }
}

module fil_glue() {
    chan_at_bridges() {
        p0 = chan_pt(chan_len - 2.0);
        translate(p0 + 1.0 * chan_in(p0)) scale([1, 1, 1.5]) sphere(d = 4.2, $fn = 24);
    }
}

module fil_in_arc() {
    ext = 18 / filament_r * 180 / PI;
    a = arc_end_deg + ext;
    translate([0, 0, arc_axis_z]) rotate([0, 0, -a])
        rotate_extrude(angle = 2 * a, $fn = 360) translate([filament_r, 0]) circle(d = fil_d, $fn = 16);
}

module switch_glue() {
    rotate([0, 0, button_az]) translate([button_r, 0, 0])
        for (s = [-1, 1]) translate([0, s * (btn_w / 2 + 0.2), btn_z0 + 0.4]) scale([1.4, 1, 0.8]) sphere(d = 3.6, $fn = 20);
}

module speaker_wires() {
    rotate([0, 0, spk_term_az[0]])
        for (s = [-1, 1]) {
            z0 = spk_flange_bot_z - spk_term_h + 1.5;
            wire_path([
                [spk_flange_half - 2, s * 1.6, z0],
                [spk_flange_half - 2, s * 1.6, z0 - 5],
                [spk_flange_half + 5, s * 3.0, z0 - 11],
                [spk_flange_half + 16, s * 5.0, z0 - 15]
            ], 1.5);
        }
}

module speaker_bead() {
    translate([0, 0, spk_flange_bot_z - 0.6]) linear_extrude(1.8)
        difference() {
            offset(delta = 1.3) spk_outline2d();
            offset(delta = -0.3) spk_outline2d();
        }
}

module glue_puddle() {
    translate([tr_x, tr_y + 7, tray_floor_t - 0.01]) scale([1, 1.4, 1]) cylinder(d = 14, h = 0.9, $fn = 48);
}

module cap_legs() {
    p = polar(cap_r, cap_az);
    hx = amp_x - amp_w / 2 + amp_hdr_in + amp_hdr_w / 2;
    for (k = [0, 1]) {
        a = [hx, amp_y - amp_hdr_pitch * (3 - k), amp_z0 + amp_t + amp_hdr_h + 1.5];
        b = [p[0] + (k - 0.5) * 3.0, p[1] + 7.0, tray_floor_t + 1.0];
        wire_path([a, [a[0], a[1], b[2] + 2], b], 0.8);
    }
}

module cap_with_legs() {
    dummy_cap();
    cap_legs();
}

module transistor_harness() {
    translate([tr_x, tr_y, tray_floor_t]) {
        translate([-5.1, 0, 0]) cube([10.2, 9.2, 4.5]);
        translate([-5.1, 9.2 - 0.01, 0]) cube([10.2, 6.5, 1.3]);
        for (k = [-1 : 1])
            translate([k * 2.54 - 0.4, -12, 0]) cube([0.8, 12.02, 0.6]);
        translate([-3.5, -9, 0.9]) rotate([0, 90, 90]) cylinder(d = 2.4, h = 6.3, $fn = 20);
    }
}

function pin_y(k) = dk_y0() + (dk_l - dk_pin_l) / 2 + (k + 0.5) * 2.54;

module pin_tip(sx, k) {
    translate([sx * dk_pin_row_x - 1.5, pin_y(k) - 1.1, dk_z1 + dk_pin_h - 3.2]) cube([3.0, 2.2, 3.4]);
}

module check_pins() {
    pin_tip(-1, 13);
    pin_tip(-1, 14);
    pin_tip(1, 13);
    pin_tip(1, 14);
}

module pin_stub(sx, k, o) {
    tip = [sx * dk_pin_row_x, pin_y(k) + o, dk_z1 + dk_pin_h - 0.3];
    wire_path([tip, tip + [sx * 2.5, 0, 1.4], tip + [sx * 13, 0, 1.8]], 0.8);
}

module devkit_wires() {
    for (k = [5 : 9]) pin_stub(-1, k, 0);
    for (o = [-0.5, 0.5]) pin_stub(-1, 13, o);
    for (o = [-0.7, 0, 0.7]) pin_stub(-1, 14, o);
    for (k = [1, 5, 10]) pin_stub(1, k, 0);
    for (o = [-0.5, 0.5]) pin_stub(1, 13, o);
    pin_stub(1, 14, 0);
}

module harness_wires() {
    for (o = [-0.6, 0.6])
        wire_path([[tr_x, tr_y - 11.5 + o, tray_floor_t + 0.5], [tr_x, tr_y - 11.5 + o, tray_floor_t + 9], [tr_x - 8, tr_y - 11.5 + 2 * o, tray_floor_t + 15]], 0.9);
}

module speaker_terminal_wires() {
    zb = amp_z0 + amp_t + amp_block_h - 0.2;
    for (o = [-2, 2])
        wire_path([[amp_x + amp_w / 2 - amp_block_w / 2, o, zb], [amp_x + amp_w / 2 - amp_block_w / 2, o, zb + 8],
            [amp_x + amp_w / 2 - amp_block_w / 2 + 4, o * 1.5, zb + 14]], 0.9);
}

module body_phase(upto, col) {
    color(col) {
        body();
        if (upto >= 1) boss_inserts();
        if (upto >= 2) post_inserts();
        if (upto >= 3) dummy_mic();
        if (upto >= 4) mic_bead();
    }
}

module arcs_phase(col, fil) {
    color(col) asm_ring();
    color(fil) dummy_filament();
}

module top_phase(upto, col) {
    color(col) {
        top(grille = true);
        if (upto >= 10) top_inserts();
        if (upto >= 11) button_cap();
        if (upto >= 12) dummy_button();
        if (upto >= 13) switch_glue();
        if (upto >= 14) speaker_gasket();
        if (upto >= 15) dummy_speaker();
        if (upto >= 16) dummy_metal_washers();
        if (upto >= 17) dummy_speaker_screws();
        if (upto >= 18) speaker_wires();
        if (upto >= 19) speaker_bead();
    }
}

module tray_phase(upto, col, cap = true) {
    color(col) {
        tray();
        if (upto >= 23) dummy_feet();
        if (upto >= 24) tray_inserts();
        if (upto >= 25) dummy_devkit();
        if (upto >= 26) dummy_devkit_screws();
        if (upto >= 27) dummy_amp();
        if (upto >= 27 && cap) cap_with_legs();
        if (upto >= 28) {
            transistor_harness();
            glue_puddle();
        }
    }
}

module tray_assembled(col) {
    color(col) {
        tray();
        dummy_feet();
        tray_inserts();
        dummy_devkit();
        dummy_devkit_screws();
        dummy_amp();
        transistor_harness();
        glue_puddle();
        cap_with_legs();
    }
}

module closed_body(col) {
    body_phase(4, col);
    arcs_phase(col, wire_color);
    color(col) {
        fil_wires();
        fil_glue();
    }
}

module scene_1() {
    flip() {
        old() body();
        new() boss_inserts(-insert_lift);
    }
}

module scene_2() {
    old() {
        body();
        boss_inserts();
    }
    new() post_inserts(insert_lift);
}

module scene_3() {
    body_phase(2, old_color);
    new() mic_moved(mic_lift);
}

module scene_4() {
    body_phase(3, old_color);
    new() mic_bead();
}

module scene_5() {
    rotate([0, 0, -90]) {
        old() print_ring_arc();
        translate([0, 0, 13]) new() fil_in_arc();
    }
}

module scene_6() {
    body_phase(4, old_color);
    translate([0, 0, arc_lift]) arcs_phase(new_color, wire_color);
}

module scene_7() {
    body_phase(4, old_color);
    old() asm_ring();
    new() dummy_filament();
}

module scene_8() {
    body_phase(4, old_color);
    arcs_phase(old_color, wire_color);
    new() fil_wires();
}

module scene_9() {
    body_phase(4, old_color);
    arcs_phase(old_color, wire_color);
    old() fil_wires();
    new() fil_glue();
}

module scene_10() {
    flip() top_phase(9, old_color);
    flip(part_lift) new() top_inserts();
}

module scene_11() {
    flip() top_phase(10, old_color);
    flip(part_lift) new() button_cap();
}

module scene_12() {
    flip() top_phase(11, old_color);
    flip() new() dummy_button();
}

module scene_13() {
    flip() top_phase(12, old_color);
    flip() new() switch_glue();
}

module scene_14() {
    flip() top_phase(13, old_color);
    flip(part_lift) new() speaker_gasket();
}

module scene_15() {
    flip() top_phase(14, old_color);
    flip(2.2 * part_lift) new() dummy_speaker();
}

module scene_16() {
    flip() top_phase(15, old_color);
    flip(part_lift) new() dummy_metal_washers();
}

module scene_17() {
    flip() top_phase(16, old_color);
    flip(part_lift) new() dummy_speaker_screws();
}

module scene_18() {
    flip() top_phase(17, old_color);
    flip() new() speaker_wires();
}

module scene_19() {
    flip() top_phase(18, old_color);
    flip() new() speaker_bead();
}

module scene_20() {
    closed_body(old_color);
    translate([0, 0, top_lift]) top_phase(19, new_color);
}

module scene_21() {
    closed_body(old_color);
    top_phase(19, old_color);
    translate([0, 0, screw_lift]) new() dummy_top_screws();
}

module scene_22() {
    closed_body(old_color);
    top_phase(19, old_color);
    old() dummy_top_screws();
    translate([0, 0, plug_lift]) new() top_plugs();
}

module scene_23() {
    flip_tray() tray_phase(22, old_color);
    flip_tray(part_lift) new() dummy_feet();
}

module scene_24() {
    tray_phase(23, old_color);
    new() tray_inserts(part_lift);
}

module scene_25() {
    tray_phase(24, old_color);
    translate([0, 0, part_lift]) new() dummy_devkit();
}

module scene_26() {
    tray_phase(25, old_color);
    translate([0, 0, part_lift]) new() dummy_devkit_screws();
}

module scene_27() {
    tray_phase(26, old_color);
    translate([0, 0, part_lift]) new() {
        dummy_amp();
        cap_with_legs();
    }
}

module scene_28() {
    tray_phase(27, old_color);
    new() {
        transistor_harness();
        glue_puddle();
    }
}

module scene_29() {
    tray_phase(28, old_color, cap = false);
    new() cap_with_legs();
}

module scene_30() {
    tray_phase(29, old_color);
    old() harness_wires();
    new() devkit_wires();
}

module scene_31() {
    tray_phase(29, old_color);
    old() {
        harness_wires();
        devkit_wires();
    }
    new() speaker_terminal_wires();
}

module scene_32() {
    tray_phase(29, old_color);
    new() check_pins();
}

module scene_33() {
    closed_body(old_color);
    top_phase(19, old_color);
    old() dummy_top_screws();
    old() top_plugs();
    translate([0, 0, -tray_drop]) {
        tray_assembled(old_color);
        new() tray_gasket();
    }
    translate([0, 0, -tray_drop - tray_screw_drop]) new() dummy_tray_screws();
}

module scene() {
    if (step == 1) scene_1();
    else if (step == 2) scene_2();
    else if (step == 3) scene_3();
    else if (step == 4) scene_4();
    else if (step == 5) scene_5();
    else if (step == 6) scene_6();
    else if (step == 7) scene_7();
    else if (step == 8) scene_8();
    else if (step == 9) scene_9();
    else if (step == 10) scene_10();
    else if (step == 11) scene_11();
    else if (step == 12) scene_12();
    else if (step == 13) scene_13();
    else if (step == 14) scene_14();
    else if (step == 15) scene_15();
    else if (step == 16) scene_16();
    else if (step == 17) scene_17();
    else if (step == 18) scene_18();
    else if (step == 19) scene_19();
    else if (step == 20) scene_20();
    else if (step == 21) scene_21();
    else if (step == 22) scene_22();
    else if (step == 23) scene_23();
    else if (step == 24) scene_24();
    else if (step == 25) scene_25();
    else if (step == 26) scene_26();
    else if (step == 27) scene_27();
    else if (step == 28) scene_28();
    else if (step == 29) scene_29();
    else if (step == 30) scene_30();
    else if (step == 31) scene_31();
    else if (step == 32) scene_32();
    else if (step == 33) scene_33();
    else assert(false, str("unknown step: ", step));
}

scene();
