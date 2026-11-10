include <params.scad>
use <lib.scad>

eps = 0.01;

module box(x0, x1, y0, y1, z0, z1) {
    translate([x0, y0, z0]) cube([x1 - x0, y1 - y0, z1 - z0]);
}

module screw_m25(zh) {
    translate([0, 0, zh]) {
        cylinder(d = screw_head_d, h = screw_rim_h, $fn = 32);
        translate([0, 0, screw_rim_h - eps])
            cylinder(d1 = screw_head_d, d2 = screw_d,
                h = (screw_head_d - screw_d) / 2 + eps, $fn = 32);
        translate([0, 0, screw_head_h - eps])
            cylinder(d = screw_d, h = screw_len - screw_head_h + eps, $fn = 24);
    }
}

module at_polar(r, a) {
    let(p = polar(r, a)) translate([p[0], p[1], 0]) children();
}

module dummy_devkit() {
    y0 = dk_y0();
    y1 = dk_y1();
    pin_y0 = y0 + (dk_l - dk_pin_l) / 2;
    union() {
        difference() {
            box(-dk_w / 2, dk_w / 2, y0, y1, dk_z0, dk_z1);
            for (sx = [-1, 1], y = [y0 + dk_hole_in, y1 - dk_hole_in])
                translate([sx * dk_hole_x, y, dk_z0 - 1]) cylinder(d = dk_hole_d, h = dk_t + 2, $fn = 32);
        }
        box(-dk_module_w / 2, dk_module_w / 2, y0, y0 + dk_module_l,
            dk_z0 - dk_parts_h, dk_z0 + eps);
        box(-dk_usb_w / 2, dk_usb_w / 2, y1 - dk_usb_body_l, usb_tip_y(),
            dk_z0 - dk_usb_h, dk_z0 + eps);
        for (s = [-1, 1])
            box(s * dk_pin_row_x - dk_pin_w / 2, s * dk_pin_row_x + dk_pin_w / 2,
                pin_y0, pin_y0 + dk_pin_l, dk_z1 - eps, dk_z1 + dk_pin_h);
    }
}

module dummy_usb_plug() {
    box(-usb_plug_w / 2, usb_plug_w / 2, usb_tip_y(), usb_tip_y() + usb_plug_len,
        usb_z - usb_plug_h / 2, usb_z + usb_plug_h / 2);
}

module dummy_speaker() {
    magnet_top = spk_magnet_bot_z + spk_magnet_h;
    union() {
        translate([0, 0, spk_flange_bot_z])
            linear_extrude(spk_flange_t) difference() {
                spk_outline2d();
                for (a = spk_hole_angles)
                    translate(polar(spk_hole_r, a)) spk_hole_slot2d(a);
            }
        translate([0, 0, magnet_top - eps])
            cylinder(r1 = spk_magnet_d / 2 + 1, r2 = spk_basket_top_d / 2,
                h = spk_flange_bot_z - magnet_top + 2 * eps, $fn = 96);
        translate([0, 0, spk_magnet_bot_z])
            cylinder(d = spk_magnet_d, h = spk_magnet_h, $fn = 96);
        translate([0, 0, spk_low_z])
            cylinder(d = spk_magnet_d, h = spk_yoke_t + eps, $fn = 96);
        translate([0, 0, spk_flange_top_z - eps])
            difference() {
                cylinder(d = spk_surround_d, h = spk_surround_h + eps, $fn = 120);
                translate([0, 0, -1])
                    cylinder(d = spk_surround_d - 2 * spk_surround_w, h = spk_surround_h + 3, $fn = 120);
            }
        for (a = spk_term_az)
            rotate([0, 0, a])
                box(spk_flange_half - spk_term_in, spk_flange_half + spk_term_out,
                    -spk_term_w / 2, spk_term_w / 2,
                    spk_flange_bot_z - spk_term_h, spk_flange_bot_z + eps);
    }
}

module dummy_amp() {
    x0 = amp_x - amp_w / 2;
    x1 = amp_x + amp_w / 2;
    zt = amp_z0 + amp_t;
    hx = x0 + amp_hdr_in + amp_hdr_w / 2;
    hl = amp_hdr_n * amp_hdr_pitch;
    union() {
        box(x0, x1, amp_y - amp_l / 2, amp_y + amp_l / 2, amp_z0, zt);
        box(x1 - amp_block_w, x1, amp_y - amp_block_l / 2, amp_y + amp_block_l / 2,
            zt - eps, zt + amp_block_h);
        box(x0 + amp_hdr_in, x0 + amp_hdr_in + amp_hdr_w, amp_y - hl / 2, amp_y + hl / 2,
            zt - eps, zt + amp_hdr_h);
        for (k = [0 : amp_hdr_n - 1])
            box(hx - amp_hdr_pin_d / 2, hx + amp_hdr_pin_d / 2,
                amp_y + (k - (amp_hdr_n - 1) / 2) * amp_hdr_pitch - amp_hdr_pin_d / 2,
                amp_y + (k - (amp_hdr_n - 1) / 2) * amp_hdr_pitch + amp_hdr_pin_d / 2,
                amp_z0 - amp_hdr_tail, zt + amp_hdr_h + amp_hdr_pin);
    }
}

module dummy_cap() {
    at_polar(cap_r, cap_az) rotate([0, 0, cap_az]) translate([0, -cap_l / 2, tray_floor_t + cap_d / 2])
        rotate([-90, 0, 0]) cylinder(d = cap_d, h = cap_l, $fn = 48);
}

module dummy_antenna() {
    box(-ant_w / 2, ant_w / 2, dk_y0(), dk_y0() + ant_len, dk_z0 - dk_parts_h, antenna_top_z);
}

module dummy_wire_keepout() {
    pin_y0 = dk_y0() + (dk_l - dk_pin_l) / 2;
    hx = amp_x - amp_w / 2 + amp_hdr_in + amp_hdr_w / 2;
    hl = amp_hdr_n * amp_hdr_pitch;
    for (s = [-1, 1])
        box(s * dk_pin_row_x - dk_pin_w / 2, s * dk_pin_row_x + dk_pin_w / 2,
            pin_y0, pin_y0 + dk_pin_l, dk_pin_top_z, dk_pin_top_z + wire_clear_h);
    box(hx - amp_hdr_w / 2, hx + amp_hdr_w / 2, amp_y - hl / 2, amp_y + hl / 2,
        amp_pin_top_z, amp_pin_top_z + wire_clear_h);
}

module dummy_mic() {
    rotate([0, 0, mic_az]) translate([mic_face_r(), 0, mic_z]) {
        translate([-mic_t, 0, 0]) rotate([0, 90, 0]) cylinder(d = mic_d, h = mic_t, $fn = 64);
        box(-mic_t - mic_can_h + eps, -mic_t + eps, -mic_can_w / 2, mic_can_w / 2,
            -mic_can_l / 2, mic_can_l / 2);
        for (sy = [-1, 1], k = [-1 : 1])
            translate([-mic_t - mic_pin_head, sy * mic_pin_row, k * mic_pin_pitch])
                rotate([0, 90, 0])
                    cylinder(d = mic_pin_d, h = mic_pin_head + mic_t + mic_pin_tail, $fn = 16);
    }
}

module dummy_button(square = btn_stem_square) {
    rotate([0, 0, button_az]) translate([button_r, 0, 0]) {
        box(-btn_w / 2, btn_w / 2, -btn_w / 2, btn_w / 2, btn_z0, btn_z0 + btn_body_h);
        if (square)
            box(-btn_stem_sq / 2, btn_stem_sq / 2, -btn_stem_sq / 2, btn_stem_sq / 2,
                btn_z0 + btn_body_h - eps, btn_z0 + btn_h);
        else
            translate([0, 0, btn_z0 + btn_body_h - eps])
                cylinder(d = btn_pusher_d, h = btn_h - btn_body_h + eps, $fn = 32);
        for (sx = [-1, 1], sy = [-1, 1])
            box(sx * btn_leg_x - btn_leg_t / 2, sx * btn_leg_x + btn_leg_t / 2,
                sy * btn_leg_y - btn_leg_w / 2, sy * btn_leg_y + btn_leg_w / 2,
                btn_z0 - (sx < 0 ? btn_leg_l : btn_stub_l), btn_z0 + eps);
    }
}

module contact_strip() {
    c = chan_dir;
    a = unit(chan_in(chan_pt(fil_excess)) - (chan_in(chan_pt(fil_excess)) * c) * c);
    b = cross(c, a);
    p = chan_pt(fil_excess);
    multmatrix([[a[0], b[0], c[0], p[0]], [a[1], b[1], c[1], p[1]], [a[2], b[2], c[2], p[2]], [0, 0, 0, 1]])
        translate([-fil_contact_t / 2, -fil_contact_w / 2, 0]) cube([fil_contact_t, fil_contact_w, fil_contact]);
}

module dummy_filament() {
    lit = 180 - 2 * bridge_half_deg;
    for (base = bridge_angles)
        translate([0, 0, fil_z]) rotate([0, 0, base + 90 - lit / 2])
            rotate_extrude(angle = lit, $fn = 360)
                translate([filament_r, 0]) circle(d = fil_d, $fn = 16);
    chan_at_bridges() {
        chan_tube(fil_d, fil_excess, 0, 16);
        contact_strip();
    }
}

module dummy_tray_screws() {
    for (a = tray_screw_angles) at_polar(tray_screw_r, a) screw_m25(tray_screw_head_z);
}

module dummy_devkit_screws() {
    for (sx = [-1, 1])
        translate([sx * dk_hole_x, dk_y0() + dk_hole_in, dk_screw_head_z]) mirror([0, 0, 1]) screw_m25(0);
}

module dummy_top_screws() {
    for (a = top_screw_angles)
        at_polar(top_screw_r, a) translate([0, 0, top_screw_head_z]) mirror([0, 0, 1]) screw_m25(0);
}

module dummy_speaker_screws() {
    for (a = spk_hole_angles)
        at_polar(spk_hole_r, a) screw_m25(spk_screw_head_z);
}

module dummy_metal_washers() {
    for (a = spk_hole_angles)
        at_polar(spk_hole_r, a) translate([0, 0, spk_washer_bot_z]) difference() {
            cylinder(d = washer_od, h = washer_t, $fn = 48);
            translate([0, 0, -1]) cylinder(d = washer_id, h = washer_t + 2, $fn = 32);
        }
}

module dummy_feet() {
    for (a = foot_angles)
        rotate([0, 0, a]) translate([foot_r, 0, tray_bot_z - foot_out]) linear_extrude(foot_h) foot2d();
}

module probe_mic_port() {
    rotate([0, 0, mic_az]) translate([mic_face_r(), 0, mic_z]) rotate([0, 90, 0])
        cylinder(d = mic_probe_d, h = body_r(mic_z) + 2 - mic_face_r(), $fn = 24);
}

module probe_filament_channel() {
    chan_at_bridges() chan_tube(chan_probe_d, chan_len, 0, 16);
}
