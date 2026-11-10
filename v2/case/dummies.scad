include <params.scad>
use <lib.scad>

eps = 0.01;

module dummy_speaker() {
    union() {
        translate([0, 0, spk_flange_bot_z]) linear_extrude(spk_flange_t) difference() {
            union() {
                circle(d = spk_flange_d, $fn = 128);
                for (m = [0, 1]) mirror([m, 0]) hull() {
                    translate([spk_ear_pitch / 2, 0]) circle(r = spk_ear_tip_r, $fn = 64);
                    translate([spk_ear_base_x, -spk_ear_base_w / 2]) square([eps, spk_ear_base_w]);
                }
            }
            for (s = [-1, 1]) translate([s * spk_ear_pitch / 2, 0]) circle(d = spk_ear_hole_d, $fn = 32);
        }
        translate([0, 0, spk_ear_z - eps]) difference() {
            cylinder(d = spk_flange_d - 2 * eps, h = spk_rim_h + eps, $fn = 128);
            translate([0, 0, -1]) cylinder(d = spk_flange_d - 2 * spk_rim_w, h = spk_rim_h + 2, $fn = 128);
        }
        translate([0, 0, spk_ear_z - eps]) cylinder(d = spk_open_d, h = spk_cap_top_z - spk_ear_z + eps, $fn = 128);
        translate([0, 0, spk_magnet_top_z - eps])
            cylinder(d1 = spk_magnet_d, d2 = spk_basket_d, h = spk_flange_bot_z - spk_magnet_top_z + 2 * eps, $fn = 128);
        translate([0, 0, spk_mag_z]) cylinder(d = spk_magnet_d, h = spk_magnet_h, $fn = 128);
        box(-spk_term_w / 2, spk_term_w / 2, -(spk_basket_d / 2 + spk_term_out), -(spk_basket_d / 2 - spk_term_in),
            spk_term_z0, spk_term_z1);
    }
}

module dummy_usb_socket(x) {
    difference() {
        box(x - dk_usb_w / 2, x + dk_usb_w / 2, usb_face_y, usb_face_y + dk_usb_len,
            usb_z - dk_usb_h / 2, usb_z + dk_usb_h / 2);
        box(x - usb_shell_w / 2 - usb_cav_clr, x + usb_shell_w / 2 + usb_cav_clr,
            usb_face_y - 1, usb_face_y + usb_shell_l + usb_cav_clr,
            usb_z - usb_shell_h / 2 - usb_cav_clr, usb_z + usb_shell_h / 2 + usb_cav_clr);
    }
}

module dummy_devkit() {
    union() {
        box(-dk_w / 2, dk_w / 2, dk_y0, dk_y1, dk_z0, dk_z1);
        for (s = [-1, 1]) {
            box(s * dk_row_x - dk_hdr_w / 2, s * dk_row_x + dk_hdr_w / 2,
                dk_pin_y0 - dk_pin_pitch / 2, dk_pin_y1 + dk_pin_pitch / 2, dk_hdr_z0, dk_z0 + eps);
            for (k = [0 : dk_pins - 1])
                box(s * dk_row_x - dk_pin_sq / 2, s * dk_row_x + dk_pin_sq / 2,
                    dk_pin_y0 + k * dk_pin_pitch - dk_pin_sq / 2, dk_pin_y0 + k * dk_pin_pitch + dk_pin_sq / 2,
                    dk_pin_bot_z, dk_pin_top_z);
        }
        box(-dk_mod_w / 2, dk_mod_w / 2, dk_y1 - dk_mod_l, dk_y1, dk_z1 - eps, dk_z1 + dk_mod_h);
        dummy_usb_socket(dk_com_x);
        dummy_usb_socket(dk_usb2_x);
    }
}

module dummy_antenna() {
    box(-dk_ant_w / 2, dk_ant_w / 2, dk_y1, dk_ant_y1, dk_z1, dk_z1 + dk_ant_t);
}

module dummy_usb_plug(w = usb_plug_w, h = usb_plug_h) {
    union() {
        box(dk_com_x - w / 2, dk_com_x + w / 2, usb_plug_face_y - usb_plug_len, usb_plug_face_y,
            usb_z - h / 2, usb_z + h / 2);
        box(dk_com_x - usb_shell_w / 2, dk_com_x + usb_shell_w / 2, usb_plug_face_y - eps, usb_shell_tip_y,
            usb_z - usb_shell_h / 2, usb_z + usb_shell_h / 2);
    }
}

module dummy_amp() {
    hw = amp_w / 2;
    hl = amp_l / 2;
    zt = amp_z0 + amp_t;
    hlen = amp_hdr_n * amp_hdr_pitch;
    translate([amp_xy[0], amp_xy[1], 0]) rotate([0, 0, amp_rot]) union() {
        box(-hw, hw, -hl, hl, amp_z0, zt);
        box(-hw, -hw + amp_block_w, -amp_block_l / 2, amp_block_l / 2, zt - eps, amp_block_top_z);
        box(hw - amp_hdr_in - amp_hdr_w, hw - amp_hdr_in, -hlen / 2, hlen / 2, zt - eps, zt + amp_hdr_h);
        for (k = [0 : amp_hdr_n - 1]) let (y = (k - (amp_hdr_n - 1) / 2) * amp_hdr_pitch)
            box(amp_hdr_lx - amp_hdr_pin_d / 2, amp_hdr_lx + amp_hdr_pin_d / 2,
                y - amp_hdr_pin_d / 2, y + amp_hdr_pin_d / 2, amp_z0 - amp_hdr_tail, amp_pin_top_z);
    }
}

module dummy_cap() {
    translate([cap_xy[0], cap_xy[1], cap_axis_z]) rotate([0, 90, 0]) cylinder(d = cap_d, h = cap_l, center = true, $fn = 64);
}

module dummy_mic(az) {
    rotate([0, 0, az]) translate([mic_face_r, 0, mic_z]) union() {
        translate([-mic_t, 0, 0]) rotate([0, 90, 0]) cylinder(d = mic_d, h = mic_t, $fn = 64);
        box(-mic_t - mic_can_h, -mic_t + eps, -mic_can_w / 2, mic_can_w / 2, -mic_can_l / 2, mic_can_l / 2);
    }
}

module dummy_button() {
    rotate([0, 0, button_az]) translate([button_r, 0, 0]) union() {
        box(-btn_w / 2, btn_w / 2, -btn_w / 2, btn_w / 2, btn_z0, btn_z0 + btn_body_h);
        translate([0, 0, btn_z0 + btn_body_h - eps])
            cylinder(d = btn_pusher_d, h = btn_h - btn_body_h + eps, $fn = 32);
        for (sx = [-1, 1], sy = [-1, 1])
            box(sx * btn_leg_x - btn_leg_t / 2, sx * btn_leg_x + btn_leg_t / 2,
                sy * btn_leg_y - btn_leg_w / 2, sy * btn_leg_y + btn_leg_w / 2,
                btn_z0 - (sx < 0 ? btn_leg_l : btn_stub_l), btn_z0 + eps);
    }
}

module dummy_filament() {
    translate([0, 0, fil_z]) rotate([0, 0, 90 - fil_ring_half])
        rotate_extrude(angle = fil_ring_deg, $fn = 360)
            translate([filament_r, 0]) circle(d = fil_d, $fn = 16);
}

module dummy_transistor() {
    y0 = -tr_len / 2;
    yb = y0 + tr_tab_l;
    ye = yb + tr_body_l;
    translate([transistor_xy[0], transistor_xy[1], tr_z0]) union() {
        box(-tr_w / 2, tr_w / 2, y0, ye, 0, tr_tab_t);
        box(-tr_w / 2, tr_w / 2, yb, ye, 0, tr_body_h);
        for (k = [-1 : 1])
            box(k * tr_lead_pitch - tr_lead_w / 2, k * tr_lead_pitch + tr_lead_w / 2,
                ye - eps, ye + tr_lead_l, tr_lead_z, tr_lead_z + tr_lead_t);
    }
}

module dummy_speaker_screws() {
    for (p = spk_ear_xy) translate([p[0], p[1], 0]) screw_m25(spk_screw_head_z);
}

module dummy_metal_washers() {
    for (p = spk_ear_xy) translate([p[0], p[1], spk_washer_bot_z]) difference() {
        cylinder(d = washer_od, h = washer_t, $fn = 48);
        translate([0, 0, -1]) cylinder(d = washer_id, h = washer_t + 2, $fn = 32);
    }
}

module dummy_top_screws() {
    for (p = top_screw_xy) translate([p[0], p[1], top_screw_head_z]) mirror([0, 0, 1]) screw_m25(0);
}

module dummy_insert() {
    difference() {
        cylinder(d = insert_d - 0.02, h = insert_len, $fn = 32);
        translate([0, 0, -1]) cylinder(d = screw_d + 0.1, h = insert_len + 2, $fn = 24);
    }
}

module dummy_spk_inserts() {
    for (p = spk_ear_xy) translate([p[0], p[1], spk_standoff_z0]) dummy_insert();
}

module dummy_column_inserts() {
    for (p = top_screw_xy) translate([p[0], p[1], top_col_z1 - insert_len]) dummy_insert();
}

module dummy_tray_inserts() {
    for (p = tray_screw_xy) translate([p[0], p[1], body_bot_z]) dummy_insert();
}

module dummy_inserts() {
    dummy_spk_inserts();
    dummy_column_inserts();
    dummy_tray_inserts();
}

module chain(pts, d, up = 0, fn = 16) {
    for (i = [0 : len(pts) - 2]) hull() {
        for (q = [pts[i], pts[i + 1]], dz = up > 0 ? [0, up] : [0])
            translate(q + [0, 0, dz]) sphere(d = d, $fn = fn);
    }
}

module dummy_filament_tails() {
    chain(fil_tail_pts, fil_d);
    chain(fil_tail_pts_m, fil_d);
}

module dummy_top_gasket() {
    translate([0, 0, top_z0]) ring_band(top_groove_out_r - 0.01, top_gasket_w - 0.01, top_gasket_c);
}

module probe_tail_drop() {
    chain(fil_tail_pts, fil_d, top_z0 + 1 - fil_z);
    chain(fil_tail_pts_m, fil_d, top_z0 + 1 - fil_z);
}

module probe_wires() {
    chain(wire_btn_pts, wire_probe_d, 0, 32);
    chain(wire_term_pts, wire_probe_d, 0, 32);
}

module probe_mic_seal(az) {
    rotate([0, 0, az]) translate([mic_face_r + 0.02, 0, mic_z]) rotate([0, 90, 0]) difference() {
        cylinder(r = mic_d / 2 - 0.2, h = 0.1, $fn = 64);
        translate([0, 0, -1]) cylinder(r = mic_port_d / 2 + 0.2, h = 2, $fn = 32);
    }
}

module probe_mic_port(az) {
    rotate([0, 0, az]) translate([mic_face_r - 0.5, 0, mic_z]) rotate([0, 90, 0])
        cylinder(d = mic_port_d - 0.4, h = case_r + 1 - mic_face_r, $fn = 24);
}

module dummy_tray_screws() {
    for (p = tray_screw_xy) translate([p[0], p[1], 0]) screw_m25(tray_screw_head_z);
}

module dummy_tray_gasket() {
    translate([0, 0, body_bot_z - tray_gasket_c]) ring_band(tray_groove_out_r - 0.01, tray_gasket_w - 0.01, tray_gasket_c);
}

module dummy_feet() {
    for (i = [0 : len(foot_xy) - 1])
        translate([foot_xy[i][0], foot_xy[i][1], -foot_out]) rotate([0, 0, foot_rot[i]])
            linear_extrude(foot_h) foot2d();
}

module probe_tray_screws() {
    for (p = tray_screw_xy) translate([p[0], p[1], -foot_out])
        cylinder(d = screw_head_d, h = tray_screw_head_z - 0.01 + foot_out, $fn = 48);
}

module probe_tray_wires() {
    for (i = [1 : len(tray_wire_pts) - 1]) hull() {
        for (q = [tray_wire_pts[0], tray_wire_pts[i]]) translate(q) sphere(d = wire_probe_d, $fn = 32);
    }
}
