include <params.scad>

function polar(r, a) = [r * cos(a), r * sin(a)];

function unit(v) = v / norm(v);

module box(x0, x1, y0, y1, z0, z1) {
    translate([x0, y0, z0]) cube([x1 - x0, y1 - y0, z1 - z0]);
}

module at_polar(r, a) {
    let (p = polar(r, a)) translate([p[0], p[1], 0]) children();
}

module rounded_square2d(size, r, fn = 48) {
    offset(r = r, $fn = fn) square([size[0] - 2 * r, size[1] - 2 * r], center = true);
}

module insert_hole(depth = insert_hole_depth) {
    translate([0, 0, -depth]) cylinder(d = insert_d, h = depth + 0.01, $fn = 32);
}

module screw_seat() {
    translate([0, 0, -1]) cylinder(d = csk_d, h = csk_bore_h + 1, $fn = 48);
    translate([0, 0, csk_bore_h - 0.01])
        cylinder(d1 = csk_d, d2 = screw_thru_d, h = (csk_d - screw_thru_d) / 2 + 0.01, $fn = 48);
}

module screw_m25(zh, eps = 0.01) {
    translate([0, 0, zh]) {
        cylinder(d = screw_head_d, h = screw_rim_h, $fn = 32);
        translate([0, 0, screw_rim_h - eps])
            cylinder(d1 = screw_head_d, d2 = screw_d, h = (screw_head_d - screw_d) / 2 + eps, $fn = 32);
        translate([0, 0, screw_head_h - eps])
            cylinder(d = screw_d, h = screw_len - screw_head_h + eps, $fn = 24);
    }
}

module foot2d(grow = 0) {
    offset(r = foot_corner_r + grow, $fn = 48) square([foot_l - 2 * foot_corner_r, foot_w - 2 * foot_corner_r], center = true);
}

module ring_band(r_out, w, h, fn = 360) {
    rotate_extrude($fn = fn) translate([r_out - w, 0]) square([w, h]);
}
