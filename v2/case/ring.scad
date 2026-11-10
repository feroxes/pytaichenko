include <params.scad>
use <lib.scad>

ring_fn = 360;
ring_arc_steps = 36;

ring_slit_th0 = asin(sqrt(ring_pocket_r * ring_pocket_r - ring_slit_out * ring_slit_out) / ring_pocket_r);
ring_slit_th1 = 180 - asin(sqrt(ring_pocket_r * ring_pocket_r - ring_slit_in * ring_slit_in) / ring_pocket_r);

function ring_pt(p) = [filament_r + p[0], ring_axis_h + p[1]];

function ring_groove_pts() = concat(
    [ring_pt([ring_slit_out, ring_arc_h])],
    [for (i = [0 : ring_arc_steps])
        let (th = ring_slit_th0 - i * (360 - (ring_slit_th1 - ring_slit_th0)) / ring_arc_steps)
            ring_pt([ring_pocket_r * cos(th), ring_pocket_r * sin(th)])],
    [ring_pt([-ring_slit_in, ring_arc_h])]
);

function ring_lip_pts() = [
    [ring_in_r, ring_arc_h - ring_lip_lap],
    [ring_lip_out_r, ring_arc_h - ring_lip_lap],
    [ring_lip_out_r, ring_arc_h + ring_lip_h - ring_lip_chamfer],
    [ring_lip_out_r - ring_lip_chamfer, ring_arc_h + ring_lip_h],
    [ring_in_r, ring_arc_h + ring_lip_h]
];

module ring_profile() {
    difference() {
        union() {
            translate([ring_in_r, 0]) square([ring_outer_r - ring_in_r, ring_arc_h]);
            polygon(ring_lip_pts());
        }
        polygon(ring_groove_pts());
    }
}

module ring_arc_flat() {
    rotate([0, 0, -fil_ring_half]) rotate_extrude(angle = fil_ring_deg, $fn = ring_fn) ring_profile();
}

module print_ring_arc() {
    ring_arc_flat();
}

module ring_arc() {
    rotate([0, 0, 90]) translate([0, 0, ring_top_z]) rotate([180, 0, 0]) ring_arc_flat();
}
