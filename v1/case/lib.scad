include <params.scad>

function body_r(z) = z <= plinth_h ? plinth_r
    : z < edge_zt ? heel_c[0] + sqrt(max(0, heel_r * heel_r - pow(heel_c[1] - z, 2)))
    : bowl_r(z);

function body_dr(z) = z <= plinth_h ? 0
    : z < edge_zt ? (heel_c[1] - z) / sqrt(max(1e-9, heel_r * heel_r - pow(heel_c[1] - z, 2)))
    : bowl_dr(z);

function body_inner_r(z) = body_r(z) - wall_t;

function usb_tip_y_max() = sqrt(pow(body_inner_r(0) - usb_open_clear, 2) - pow(dk_usb_w / 2, 2));

function usb_tip_y() = min(body_inner_r(usb_z) - usb_tip_inset, usb_tip_y_max());

function dk_y1() = usb_tip_y() - dk_usb_out;

function dk_y0() = dk_y1() - dk_l;

function mic_face_r() = body_inner_r(mic_z) - mic_boot_t;

function polar(r, a) = [r * cos(a), r * sin(a)];

module screw_seat() {
    translate([0, 0, -1]) cylinder(d = csk_d, h = csk_bore_h + 1, $fn = 48);
    translate([0, 0, csk_bore_h - 0.01])
        cylinder(d1 = csk_d, d2 = screw_thru_d, h = (csk_d - screw_thru_d) / 2 + 0.01, $fn = 48);
}

module spk_outline2d(inset = 0) {
    if (inset == 0)
        polygon(spk_outline_pts());
    else
        offset(r = -inset, $fn = 96) polygon(spk_outline_pts());
}

module spk_hole_slot2d(a, d = spk_hole_d, l = spk_hole_l) {
    rotate(a + spk_hole_slot_rel) hull() {
        translate([(l - d) / 2, 0]) circle(d = d, $fn = 32);
        translate([-(l - d) / 2, 0]) circle(d = d, $fn = 32);
    }
}

module foot2d(grow = 0) {
    offset(r = foot_corner_r + grow, $fn = 48) square([foot_l - 2 * foot_corner_r, foot_w - 2 * foot_corner_r], center = true);
}

chan_steps = 12;

function chan_us(l) = concat([for (i = [0 : chan_steps]) if (chan_bend_len * i / chan_steps < l) chan_bend_len * i / chan_steps], [l]);

function chan_in(p) = [-p[0], -p[1], 0] / norm([p[0], p[1]]);

module chan_tube(d, l = chan_len, slot = 0, fn = 24, up = 0) {
    us = chan_us(l);
    for (i = [0 : len(us) - 2]) hull() for (u = [us[i], us[i + 1]], h = up > 0 ? [0, up] : [0]) {
        translate(chan_pt(u) + [0, 0, h]) sphere(d = d, $fn = fn);
        if (slot > 0) translate(chan_pt(u) + [0, 0, h] + slot * chan_in(chan_pt(u))) sphere(d = d, $fn = fn);
    }
}

module chan_at_bridges() {
    for (b = bridge_angles, m = [0, 1]) rotate([0, 0, b]) mirror([0, m, 0]) children();
}
