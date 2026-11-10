include <params.scad>
use <lib.scad>
use <body.scad>

small_fn = 96;

bcap_stem_r = (button_hole_d - 2 * clr) / 2;
bcap_flange_r = btn_flange_d / 2;
bcap_socket_r = (btn_pusher_d + 2 * clr) / 2;
bcap_socket_sq = btn_stem_sq + 2 * clr;
bcap_flange_h = 1.5;
bcap_cone_h = 1.0;
bcap_top_chamfer = 0.4;
bcap_z0 = btn_tip_z - bcap_flange_h;
bcap_straight_h = bcap_flange_h - bcap_cone_h;

foot_chamfer_r = 0.6;
foot_chamfer_h = 0.8;

gasket_hole_d = screw_thru_d;
gasket_fn = 180;
lip_arc_n = 24;

mb_fn = 96;
mb_rev_fn = 180;

function mb_back_r(z) = body_inner_r(z) + mic_seat_depth - mb_back_gap;

mb_relief_r = mic_pin_d / 2 + mb_relief_clr;
mb_relief_floor_t = sqrt(pow(mb_back_r(mic_z - mic_pin_half - mb_relief_r), 2)
    - pow(mic_pin_row + mb_relief_r, 2)) - (mic_face_r() + mb_relief_depth);

mb_clip_cands = [for (z = [mic_z - mb_env_half : 0.1 : mic_z])
    if (mb_back_r(z) - mic_face_r() >= mb_min_t) z];
mb_z_clip = min(concat(mb_clip_cands, [mic_z]));
mb_tilt = atan(body_dr(mic_z));
mb_print_x = mic_z * cos(mb_tilt) + mb_back_r(mic_z) * sin(mb_tilt);
mb_print_z = mb_back_r(mic_z) * cos(mb_tilt) - mic_z * sin(mb_tilt);

module bcap_local() {
    difference() {
        union() {
            translate([0, 0, bcap_z0])
                cylinder(r = bcap_flange_r, h = bcap_straight_h, $fn = small_fn);
            translate([0, 0, bcap_z0 + bcap_straight_h])
                cylinder(r1 = bcap_flange_r, r2 = bcap_stem_r, h = bcap_cone_h, $fn = small_fn);
            translate([0, 0, btn_tip_z])
                cylinder(r = bcap_stem_r, h = H - bcap_top_chamfer - btn_tip_z, $fn = small_fn);
            translate([0, 0, H - bcap_top_chamfer])
                cylinder(r1 = bcap_stem_r, r2 = bcap_stem_r - bcap_top_chamfer,
                    h = bcap_top_chamfer, $fn = small_fn);
        }
        translate([0, 0, bcap_z0 - 1]) {
            cylinder(r = bcap_socket_r, h = btn_tip_z - bcap_z0 + 1, $fn = small_fn);
            translate([-bcap_socket_sq / 2, -bcap_socket_sq / 2, 0])
                cube([bcap_socket_sq, bcap_socket_sq, btn_tip_z - bcap_z0 + 1]);
        }
    }
}

module button_cap() {
    rotate([0, 0, button_az]) translate([button_r, 0, 0]) bcap_local();
}

module print_button_cap() {
    translate([0, 0, H]) rotate([180, 0, 0]) bcap_local();
}

module foot() {
    hull() {
        linear_extrude(0.01) offset(delta = -foot_chamfer_r) foot2d();
        translate([0, 0, foot_chamfer_h]) linear_extrude(foot_h - foot_chamfer_h) foot2d();
    }
}

module gasket_local() {
    linear_extrude(gasket_t) difference() {
        spk_outline2d();
        circle(r = gasket_inner_r, $fn = gasket_fn);
        for (a = spk_hole_angles)
            translate(polar(spk_hole_r, a)) circle(d = gasket_hole_d, $fn = 32);
    }
}

module top_plug_local() {
    d = top_plug_d + top_plug_fit;
    c = top_plug_chamfer;
    cylinder(d = d, h = top_plug_depth - c, $fn = 64);
    translate([0, 0, top_plug_depth - c - 0.01])
        cylinder(d1 = d, d2 = d - 2 * c, h = c + 0.01, $fn = 64);
}

module top_plugs() {
    for (a = top_screw_angles)
        translate([polar(top_screw_r, a)[0], polar(top_screw_r, a)[1], H]) mirror([0, 0, 1]) top_plug_local();
}

module print_top_plug() {
    top_plug_local();
}

module speaker_gasket() {
    translate([0, 0, gasket_z0]) gasket_local();
}

module print_speaker_gasket() {
    gasket_local();
}

function lip_hinge_r() = tray_flange_r() - lip_hinge_inset;

function lip_rot(q, a) = [q[0] * cos(a) - q[1] * sin(a), q[0] * sin(a) + q[1] * cos(a)];

function lip_arc_pts() = [
    for (k = [0 : lip_arc_n])
        let (a = 180 + lip_lean - 180 * k / lip_arc_n)
            [lip_vx + lip_nose_r * cos(a), lip_hc + lip_nose_r * sin(a)]
];

function lip_local_pts() = concat([[0, 0], [-lip_w_h, 0]], lip_arc_pts());

function lip_pts(phi) = [
    for (q = lip_local_pts()) let (w = lip_rot(q, phi)) [lip_hinge_r() + w[0], lip_base_t + w[1]]
];

module lip_profile(phi) {
    union() {
        translate([tray_gasket_in_r(), 0])
            square([tray_flange_r() - tray_gasket_in_r(), lip_base_t]);
        polygon(lip_pts(phi));
    }
}

module tray_gasket_local(phi) {
    rotate_extrude($fn = gasket_fn) lip_profile(phi);
}

module tray_gasket() {
    translate([0, 0, -tray_gasket_t]) tray_gasket_local(lip_phi);
}

module print_tray_gasket() {
    tray_gasket_local(0);
}

module mb_axis_cyl(d, x0, x1) {
    translate([x0, 0, mic_z]) rotate([0, 90, 0]) cylinder(d = d, h = x1 - x0, $fn = mb_fn);
}

module mb_envelope(setback) {
    intersection() {
        rotate_extrude($fn = mb_rev_fn)
            polygon(concat(
                [[0, mic_z - mb_env_half]],
                [for (z = [mic_z - mb_env_half : 1 : mic_z + mb_env_half]) [mb_back_r(z) - setback, z]],
                [[0, mic_z + mb_env_half]]
            ));
        translate([-100, -100, mb_z_clip]) cube([200, 200, 100]);
    }
}

module mb_lips(x0) {
    for (k = [0 : 3])
        translate([x0, 0, mic_z]) rotate([0, 90, 0])
            rotate([0, 0, 45 + 90 * k - mb_lip_span / 2])
                rotate_extrude(angle = mb_lip_span, $fn = mb_fn)
                    polygon([
                        [mb_lip_in_r, mb_lip_t],
                        [mb_lip_out_r, mb_lip_t],
                        [mb_lip_out_r, 0],
                        [mb_lip_in_r + mb_lip_t, 0]
                    ]);
}

module mb_pin_relief(xf) {
    translate([xf - 0.01, 0, mic_z]) rotate([90, 0, 90])
        linear_extrude(mb_relief_depth + 0.01)
            for (sy = [-1, 1])
                hull() for (s = [-1, 1])
                    translate([sy * mic_pin_row, s * mic_pin_half]) circle(r = mb_relief_r, $fn = 24);
}

module mic_boot_local() {
    assert(mb_z_clip <= mic_z - mb_channel_d / 2 - 0.3, "mic boot clip reaches the sound channel");
    assert(mb_relief_floor_t >= mb_min_t, "mic boot floor too thin behind the pin relief");
    xf = mic_face_r();
    xb = xf - mic_t - mb_lip_t;
    union() {
        difference() {
            hull() {
                intersection() {
                    mb_axis_cyl(mb_d, xb, xf + 8);
                    mb_envelope(mb_lead);
                }
                intersection() {
                    mb_axis_cyl(mb_d - 2 * mb_lead, xb, xf + 8);
                    mb_envelope(0);
                }
            }
            mb_axis_cyl(mb_cav_d, xb - 1, xf);
            mb_axis_cyl(mb_channel_d, xf - 1, xf + 10);
            mb_pin_relief(xf);
        }
        mb_lips(xb);
    }
}

module mic_boot() {
    rotate([0, 0, mic_az]) mic_boot_local();
}

module print_mic_boot() {
    translate([-mb_print_x, 0, mb_print_z]) rotate([0, -mb_tilt, 0]) rotate([0, 90, 0])
        rotate([0, 0, -mic_az]) mic_boot();
}

module echo_small() {
    echo(gasket_hole_d=gasket_hole_d, mb_clr=mb_clr, mb_d=mb_d, mb_cav_d=mb_cav_d, mb_lip_t=mb_lip_t);
    echo(mb_lip_in_r=mb_lip_in_r, mb_lip_out_r=mb_lip_out_r, mb_lip_span=mb_lip_span);
    echo(mb_back_gap=mb_back_gap, mb_lead=mb_lead, mb_min_t=mb_min_t, mb_channel_d=mb_channel_d);
    echo(mb_z_clip=mb_z_clip, mb_tilt=mb_tilt, mb_relief_floor_t=mb_relief_floor_t);
}
