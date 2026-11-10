clr = 0.2;
bed = 256;
H = 69.5;
wall_t = 2.4;
heel_r = 21;
edge_phi = 52;
heel_n = 20;
plinth_band = 8;
plinth_inset = 5.7;

ring_h = 3.0;
ring_front_t = 0.6;
ring_lip_h = 3;
ring_lip_w = 1.2;
ring_in_clr = 0.5;
fil_lit = 175;
fil_contact = 5;
fil_trans = 3;
fil_contact_w = 0.8;
fil_contact_t = 0.15;
fil_d = 1.5;
fil_bend_r_min = 5;
fil_bend_r = 5.5;
chan_d = 1.7;
chan_slot_l = 3.0;
chan_open_l = 3.0;
chan_tail_l = 1.5;
chan_tilt = 4;
bridge_w = 14;
ring_pocket_r = (fil_d + clr) / 2;

function ceil1(x) = ceil(x * 10 - 1e-6) / 10;

insert_d = 3.3;
insert_len = 4;
insert_od = 3.5;
insert_hole_depth = insert_len + 1;
boss_od = 7.4;
boss_wall = (boss_od - insert_d) / 2;
insert_tip_gap = 0.2;
min_thread = 3.0;
screw_len = 8;
screw_d = 2.5;
screw_thru_d = 2.9;
screw_head_d = 4.7;
screw_head_h = 1.5;
screw_hex = 1.5;
screw_rim_h = screw_head_h - (screw_head_d - screw_d) / 2;
csk_clr = 0.1;
csk_gap = 0.03;
csk_d = screw_head_d + 2 * csk_clr;
csk_bore_h = screw_rim_h;
csk_h = csk_bore_h + (csk_d - screw_thru_d) / 2;
csk_head_z = csk_clr - csk_gap;

function csk_stand(d) = screw_rim_h + (screw_head_d - d) / 2;

washer_id = 2.7;
washer_od = 6.0;
washer_t = 0.5;

tray_floor_t = 1.5;
tray_gap = 0.25;
tray_gap_min = 0.2;
tray_gap_max = 0.3;
tray_screw_r = 40;
tray_screw_angles = [60, 150, 270];
tray_gasket_t = 1.2;
tray_gasket_squeeze = 0.3;
tray_flange_t = 2.6;
tray_screw_recess = 1.1;
lip_t = 0.7;
lip_base_t = 0.3;
lip_lean = 40;
lip_free_h = tray_gasket_t + tray_gasket_squeeze;
lip_hinge_inset = 0.1;
lip_nose_r = lip_t / 2;
lip_w_h = lip_t / cos(lip_lean);
lip_hc = lip_free_h - lip_nose_r - lip_base_t;
lip_vx = -(lip_w_h / 2 + lip_hc * tan(lip_lean));
lip_vlen = norm([lip_vx, lip_hc]);
lip_hc_set = tray_gasket_t - lip_nose_r - lip_base_t;
lip_phi = 180 - asin(lip_hc_set / lip_vlen) - atan2(lip_hc, lip_vx);
lip_phi_stop = 180 - asin((lip_hc_set - (tray_gasket_squeeze + 0.2)) / lip_vlen) - atan2(lip_hc, lip_vx);
tray_flange_lip = 0.1;
lip_reach = lip_hinge_inset - lip_vx * cos(lip_phi) + lip_hc * sin(lip_phi);
lip_face_need = tray_flange_lip + lip_reach + lip_nose_r + 0.4;
tray_gasket_neck_clr = 0.1;
tray_bot_z = -(tray_gasket_t + tray_flange_t);
tray_pad_gap = tray_gasket_squeeze + 0.2;
tray_pad_top_z = tray_floor_t;
tray_pad_t = tray_pad_top_z + tray_pad_gap;
tray_pad_relief_d = insert_od + 1.2;
tray_pad_relief_depth = 0.8;
tray_seat_z = tray_bot_z + tray_screw_recess;
tray_screw_head_z = tray_seat_z + csk_head_z;
tray_screw_tip_z = tray_screw_head_z + screw_len;
tray_screw_engage = tray_screw_tip_z - tray_pad_t;
tray_screw_thread = min(insert_len, tray_screw_engage);
tray_boss_hole_depth = max(insert_hole_depth,
    ceil1(tray_screw_tip_z + tray_pad_gap + insert_tip_gap - tray_pad_t));
foot_w = 10;
foot_l = 6;
foot_corner_r = 2;
foot_niche_skin = 0.8;
foot_niche_h = tray_flange_t - foot_niche_skin;
foot_out = 2;
foot_h = foot_niche_h + foot_out;
foot_niche_clr = 0.3;
foot_set = 0.5;
foot_angles = [45, 135, 225, 315];

dk_w = 28.75;
dk_l = 51.4;
dk_t = 1.6;
dk_parts_h = 3.5;
dk_pin_h = 8.4;
dk_pin_row_x = 12.7;
dk_pin_w = 2.54;
dk_pin_l = 38.1;
dk_module_w = 18;
dk_module_l = 25.5;
dk_usb_w = 9.0;
dk_usb_h = 3.3;
dk_usb_body_l = 7;
dk_usb_out = 3;
dk_support_h = 5.5;
dk_z0 = tray_floor_t + dk_support_h;
dk_z1 = dk_z0 + dk_t;
usb_z = dk_z0 - dk_usb_h / 2;
usb_tip_inset = 1.0;
ant_len = 7;
ant_w = 18;
antenna_top_z = dk_z1;

usb_plug_w = 10.0;
usb_plug_h = 6.2;
usb_plug_len = 13.9;
usb_win_clr = 0.15;
usb_win_w = usb_plug_w + 2 * usb_win_clr;
usb_win_floor = 0.15;
usb_win_roof = 0.35;
usb_win_h = usb_plug_h + usb_win_floor + usb_win_roof;
usb_win_z0 = usb_z - usb_plug_h / 2 - usb_win_floor;
usb_win_z1 = usb_win_z0 + usb_win_h;
usb_open_clear = 0.3;

amp_w = 18.7;
amp_l = 17.7;
amp_t = 1.6;
amp_total_h = 10.4;
amp_block_h = amp_total_h - amp_t;
amp_hdr_n = 7;
amp_hdr_pitch = 2.54;
amp_hdr_w = 2.54;
amp_hdr_h = 2.5;
amp_hdr_pin = 6;
amp_hdr_pin_d = 0.64;
amp_hdr_in = 0.3;
amp_hdr_tail = 1.4;
amp_block_w = 7.5;
amp_block_l = 10;
amp_x = 29;
amp_y = 0;
amp_z0 = tray_floor_t + 3;
amp_clr = 0.45;

latch_k = 1.25;
latch_vg = 0.1;
latch_relief = 2.3;
latch_root_fillet = 0.6;
latch_relief_clr = 0.2;
dkf_latch_t = 1.0;
dkf_latch_ov = 0.8;
dkf_latch_w = 10;
dkb_latch_t = 1.0;
dkb_latch_ov = 0.8;
dkb_latch_w = 3.6;
amp_latch_t = 0.8;
amp_latch_ov = 0.5;
amp_latch_w = 4;
amp_latch_dx = 2.8;
amp_latch_relief = 3.2;
amp_stop_len = 2.0;
amp_seg_len = 3.0;
amp_pad = 3;
amp_hdr_pad_w = 1.2;
amp_hdr_pad_gap = 0.15;

function latch_zt(board_top, ov) = board_top + latch_vg - latch_k * clr + latch_k * (ov + clr);

function latch_strain(board_top, ov, t, c = clr, relief = latch_relief) =
    1.5 * t * (ov + c) / pow(latch_zt(board_top, ov) - (tray_floor_t - relief), 2);

dkf_latch_strain = latch_strain(dk_z1, dkf_latch_ov, dkf_latch_t);
dkb_latch_strain = latch_strain(dk_z1, dkb_latch_ov, dkb_latch_t);
amp_latch_strain = latch_strain(amp_z0 + amp_t, amp_latch_ov, amp_latch_t, amp_clr, amp_latch_relief);

cap_d = 10;
cap_l = 17;
cap_r = 36.5;
cap_az = -45;

mic_d = 13.8;
mic_t = 1.0;
mic_total_t = 2.0;
mic_can_h = mic_total_t - mic_t;
mic_can_w = 4.7;
mic_can_l = 3.8;
mic_z = 26;
mic_az = -90;
mic_port_d = 1.8;
mic_boot_t = 1.9;
mic_seat_d = mic_d + 1.2;
mic_seat_depth = 0.8;
mic_pin_n = 3;
mic_pin_pitch = 2.54;
mic_pin_row = 3.8;
mic_pin_d = 0.8;
mic_pin_tail = 1.0;
mic_pin_head = 3.0;

button_az = -70;
btn_w = 11.9;
btn_body_h = 3.0;
btn_h = 5.7;
btn_pusher_d = 6.5;
btn_tip_gap = 0.5;
btn_leg_x = 5.5;
btn_leg_y = 2.5;
btn_leg_w = 1.0;
btn_leg_t = 0.5;
btn_leg_l = 3.5;
btn_stub_l = 0.5;
btn_stem_square = false;
btn_stem_sq = 5.0;
button_hole_d = 9;
top_plate_t = 3.0;
btn_pocket_clr = 0.15;
btn_wall = 1.0;
btn_in = btn_w / 2 + btn_pocket_clr;
btn_out = btn_in + btn_wall;
btn_leg_gap = 0.5;
btn_tip_z = H - top_plate_t - btn_tip_gap;
btn_z0 = btn_tip_z - btn_h;
top_region_h = top_plate_t + btn_tip_gap + btn_h + btn_stub_l + btn_leg_gap;
top_z0 = H - top_region_h;
ring_z0 = top_z0 - ring_h;
ring_z1 = top_z0;
fil_z = ring_z0 + ring_h / 2;

spk_rot = 110;
spk_flange_mid_w = 77.9;
spk_flange_corner_w = 66.8;
spk_flange_diag = 92.7;
spk_flange_half = spk_flange_mid_w / 2;
spk_flange_max_r = spk_flange_diag / 2;
spk_seat_flange_clr = 0.8;
spk_seat_max_r = spk_flange_max_r + spk_seat_flange_clr;

button_r = spk_flange_half + spk_seat_flange_clr + btn_pocket_clr + btn_w / 2;
spk_flange_t = 0.7;
spk_hole_pitch = 59;
spk_hole_r = spk_hole_pitch * sqrt(2) / 2;
spk_hole_angles = [for (k = [0 : 3]) (spk_rot + 45 + 90 * k) % 360];
spk_hole_d = 4;
spk_hole_l = 5;
spk_hole_slot_rel = 90;
spk_surround_d = 74.5;
spk_surround_w = 6;
spk_surround_h = 4;
spk_basket_top_d = 74.8;
spk_depth = 36;
spk_magnet_d = 60;
spk_magnet_h = 10;
spk_yoke_t = 1.5;
cone_gap = 4.5;
gasket_t = 1.2;
spk_seat_gap = 0.5;
spk_term_az = [spk_rot];
spk_term_w = 6;
spk_term_in = 6;
spk_term_out = 0;
spk_term_h = 10;

function flg_step() = spk_flange_half - spk_flange_corner_w / 2;

function flg_yt(r) = sqrt(r * r - pow(r - flg_step(), 2));

function flg_k(r) = (r - spk_flange_half) / (r - flg_step() - flg_yt(r));

function flg_tip(r) = flg_k(r) * flg_yt(r) * sqrt(2) + r * (1 - flg_k(r));

function flg_secant(r0, f0, r1, f1, n) = n == 0 ? r1
    : let (r2 = r1 - f1 * (r1 - r0) / (f1 - f0))
        flg_secant(r1, f1, r2, flg_tip(r2) - spk_flange_max_r, n - 1);

spk_side_arc_r_sharp = (pow(spk_flange_corner_w / 2, 2) + pow(flg_step(), 2)) / (2 * flg_step());
spk_side_arc_r = flg_secant(spk_flange_half + 50, flg_tip(spk_flange_half + 50) - spk_flange_max_r,
    spk_side_arc_r_sharp - 5, flg_tip(spk_side_arc_r_sharp - 5) - spk_flange_max_r, 8);
spk_side_arc_cx = spk_flange_half - spk_side_arc_r;
spk_flange_arc_y = flg_yt(spk_side_arc_r);
spk_flange_arc_phi = asin(spk_flange_arc_y / spk_side_arc_r);
spk_flange_fillet_r = spk_side_arc_r * (1 - flg_k(spk_side_arc_r));
spk_flange_fillet_c = (spk_flange_max_r - spk_flange_fillet_r) / sqrt(2);

function spk_rot2(pt, a) = [pt[0] * cos(a) - pt[1] * sin(a), pt[0] * sin(a) + pt[1] * cos(a)];

function spk_outline_pts(n_side = 120, n_fil = 36) = [
    for (k = [0 : 3])
        each concat(
            [for (i = [0 : n_side])
                let (ph = -spk_flange_arc_phi + 2 * spk_flange_arc_phi * i / n_side)
                    spk_rot2([spk_side_arc_cx + spk_side_arc_r * cos(ph), spk_side_arc_r * sin(ph)], 90 * k + spk_rot)],
            [for (i = [1 : n_fil - 1])
                let (ph = spk_flange_arc_phi + (90 - 2 * spk_flange_arc_phi) * i / n_fil)
                    spk_rot2([spk_flange_fillet_c + spk_flange_fillet_r * cos(ph),
                        spk_flange_fillet_c + spk_flange_fillet_r * sin(ph)], 90 * k + spk_rot)]
        )
];

function spk_wrap(a) = a - 360 * round(a / 360);

function spk_arc_dist(pt, c, r, mid, half) =
    let (v = pt - c, th = atan2(v[1], v[0]),
        t = mid + min(half, max(-half, spk_wrap(th - mid))))
        norm(pt - (c + r * [cos(t), sin(t)]));

function spk_corner_edge_dist(pt) =
    let (q = [abs(pt[0]), abs(pt[1])])
        min(spk_arc_dist(q, [spk_side_arc_cx, 0], spk_side_arc_r, 0, spk_flange_arc_phi),
            spk_arc_dist([q[1], q[0]], [spk_side_arc_cx, 0], spk_side_arc_r, 0, spk_flange_arc_phi),
            spk_arc_dist(q, [spk_flange_fillet_c, spk_flange_fillet_c], spk_flange_fillet_r,
                45, 45 - spk_flange_arc_phi));

spk_hole_edge_dist = spk_corner_edge_dist([spk_hole_r * cos(45), spk_hole_r * sin(45)]);
spk_slot_edge_clr = min([for (s = [-1, 1])
    spk_corner_edge_dist([spk_hole_r * cos(45) + s * (spk_hole_l - spk_hole_d) / 2 * cos(45 + spk_hole_slot_rel),
        spk_hole_r * sin(45) + s * (spk_hole_l - spk_hole_d) / 2 * sin(45 + spk_hole_slot_rel)])])
    - spk_hole_d / 2;

top_underside_z = H - top_plate_t;
spk_flange_top_z = top_underside_z - cone_gap - spk_surround_h;
spk_flange_bot_z = spk_flange_top_z - spk_flange_t;
spk_surround_top_z = spk_flange_top_z + spk_surround_h;
spk_low_z = spk_flange_top_z - spk_depth;
spk_magnet_bot_z = spk_low_z + spk_yoke_t;
gasket_z0 = spk_flange_top_z;
gasket_z1 = gasket_z0 + gasket_t;
spk_rim_h = top_underside_z - gasket_z1;
gasket_inner_r = spk_surround_d / 2 + spk_seat_gap;
spk_seat_side_w = spk_flange_half - gasket_inner_r;
spk_washer_bot_z = spk_flange_bot_z - washer_t;
spk_screw_head_z = spk_washer_bot_z - csk_stand(washer_id);
spk_screw_tip_z = spk_screw_head_z + screw_len;
spk_screw_engage = spk_screw_tip_z - gasket_z1;
spk_screw_thread = min(insert_len, spk_screw_engage);
spk_insert_hole_depth = max(insert_hole_depth,
    ceil1(spk_screw_tip_z + insert_tip_gap - gasket_z1));
spk_insert_top_z = gasket_z1 + spk_insert_hole_depth;
boss_skin = H - spk_insert_top_z;
wire_clear_h = 5;
dk_pin_top_z = dk_z1 + dk_pin_h;
amp_pin_top_z = amp_z0 + amp_t + amp_hdr_h + amp_hdr_pin;
antenna_metal_gap = spk_low_z - antenna_top_z;
total_h = H - tray_bot_z + foot_out;

grille_hole_d = 4.0;
grille_pitch = 6.4;
grille_r = gasket_inner_r - grille_hole_d / 2 - 0.5;
grille_open_ratio = (PI * grille_hole_d * grille_hole_d / 4)
    / (grille_pitch * grille_pitch * sqrt(3) / 2);

top_screw_angles = [20, 200];
bridge_angles = [-90, 90];
top_screw_ledge = 3.2;
top_boss_gap = 0.3;
ring_lip_clr = 0.2;

mic_probe_d = 1.2;
chan_probe_d = 1.6;

ring_outer_r = norm([button_r + btn_in, btn_in]) + btn_wall;
plinth_r = ring_outer_r - plinth_inset;
plinth_h = plinth_band + tray_bot_z;
edge_pt = [plinth_r, plinth_h];
heel_c = [plinth_r - heel_r * sin(edge_phi), plinth_h + heel_r * cos(edge_phi)];
bowl_rr = ring_outer_r - heel_c[0];
bowl_dz = ring_z0 - heel_c[1];
bowl_rad = (bowl_rr * bowl_rr + bowl_dz * bowl_dz - heel_r * heel_r) / (2 * (bowl_rr - heel_r));
bowl_c = [ring_outer_r - bowl_rad, ring_z0];

function bowl_r(z) = bowl_c[0] + sqrt(bowl_rad * bowl_rad - pow(min(z, ring_z0) - ring_z0, 2));

function bowl_dr(z) = z >= ring_z0 ? 0 : (ring_z0 - z) / sqrt(bowl_rad * bowl_rad - pow(z - ring_z0, 2));

heel_t = heel_c + heel_r * (heel_c - bowl_c) / norm(heel_c - bowl_c);
edge_zt = heel_t[1];
edge_out_r = heel_t[0];
edge_t_phi = atan2(heel_t[0] - heel_c[0], heel_c[1] - heel_t[1]);
heel_h = edge_zt - plinth_h;
heel_step = edge_out_r - plinth_r;
foot_r = plinth_r - tray_flange_lip - foot_set - foot_l / 2;
ring_in_r = bowl_r(ring_z0 - ring_lip_h) - wall_t - clr - ring_lip_w;

top_screw_r = ring_in_r - ring_in_clr - boss_od / 2;
top_screw_ledge_z1 = ring_z0 - ring_lip_h - ring_lip_clr;
top_ledge_z0 = top_screw_ledge_z1 - top_screw_ledge;
top_plug_depth = 1.2;
top_plug_d = 6.4;
top_plug_fit = 0.1;
top_plug_chamfer = 0.3;
top_plug_rim = 0.8;
top_boss_h = 1.7;
top_seat_z = H - top_plug_depth;
top_screw_head_z = top_seat_z - csk_head_z;
top_screw_tip_z = top_screw_head_z - screw_len;
top_boss_z0 = top_underside_z - top_boss_h;
top_col_z1 = top_boss_z0 - top_boss_gap;
top_screw_engage = top_col_z1 - top_screw_tip_z;
top_screw_thread = min(insert_len, top_screw_engage);
top_insert_hole_depth = max(insert_hole_depth, ceil1(top_screw_engage + insert_tip_gap));
top_insert_bot_z = top_col_z1 - top_insert_hole_depth;
top_seat_floor_t = top_seat_z - csk_h - top_boss_z0;

filament_r = ring_outer_r - ring_front_t - ring_pocket_r;
ring_arc_len = PI * filament_r - bridge_w;
fil_excess = (fil_lit - ring_arc_len) / 2;
bridge_half_deg = 180 / PI * (bridge_w / 2) / filament_r;

function unit(v) = v / norm(v);

chan_p0 = [filament_r * cos(bridge_half_deg), filament_r * sin(bridge_half_deg), fil_z];
chan_t = [sin(bridge_half_deg), -cos(bridge_half_deg), 0];
chan_dir = let (d0 = [-sin(chan_tilt), 0, -cos(chan_tilt)]) unit(d0 - (d0 * chan_t) * chan_t);
chan_p1 = chan_p0 + fil_bend_r * (chan_t + chan_dir);
chan_bend_len = PI / 2 * fil_bend_r;
fil_straight_lit = fil_excess - chan_bend_len;
chan_straight = fil_straight_lit + fil_contact;
chan_p2 = chan_p1 + chan_straight * chan_dir;
chan_len = chan_bend_len + chan_straight;

function chan_pt(u) = u <= chan_bend_len
    ? let (th = u / fil_bend_r * 180 / PI)
        chan_p0 + fil_bend_r * sin(th) * chan_t + fil_bend_r * (1 - cos(th)) * chan_dir
    : chan_p1 + (u - chan_bend_len) * chan_dir;

chamfer_slope = 1.05;
bridge_top_z = top_z0;
chan_open_up = bridge_top_z - chan_pt(chan_open_l)[2] + chan_d;
bridge_in_r = ring_in_r;
bar_w = 4;
wedge_out_r = ring_outer_r - 1.2;
top_corbel_z0 = top_ledge_z0 - chamfer_slope * (wedge_out_r - (top_screw_r - boss_od / 2));

arc_top_in_r = ring_in_r;
top_skirt_in_r = arc_top_in_r + 0.3;
top_edge_r = 8.5;
top_edge_psi = 40;
top_bevel_l = 1.2;
top_edge_z0 = H - top_edge_r * sin(top_edge_psi) - top_bevel_l * cos(top_edge_psi);
top_face_r = ring_outer_r - top_edge_r + top_edge_r * cos(top_edge_psi)
    - top_bevel_l * sin(top_edge_psi);

function top_outer_r(z) = z >= H - top_bevel_l * cos(top_edge_psi)
    ? top_face_r + (H - z) * tan(top_edge_psi)
    : ring_outer_r - top_edge_r + sqrt(top_edge_r * top_edge_r - pow(z - top_edge_z0, 2));

dimple_d = grille_hole_d;
dimple_depth = 1.4;
dimple_r = top_face_r - dimple_d / 2 - 1.5;
dimple_keep = boss_od / 2;
top_plug_keep = top_plug_d / 2 + dimple_d / 2 + top_plug_rim;
btn_rim_min = 0.8;
btn_collar_facet = 0.02;
btn_collar_r = button_hole_d / 2 + btn_rim_min + btn_collar_facet;
btn_collar_h = 1.0;
btn_ceil_gap = 0.2;
btn_flange_d = button_hole_d - 2 * clr + 1.8;
btn_bore_d = 10.9;
btn_ceil_z = btn_z0 + btn_body_h + btn_ceil_gap;
btn_skirt_x = max(btn_in, top_skirt_in_r + 1 - button_r);
btn_hook_ov = 0.6;
btn_hook_slope = 1;
btn_hook_u = btn_in - (btn_w / 2 - btn_hook_ov);
btn_hook_z0 = btn_z0 - btn_hook_u * btn_hook_slope;
btn_tab_w = 5;
btn_tab_slit = 0.6;
btn_root_in = 0.6;
btn_root_out = 1.2;
btn_tab_flex = 0.7;
btn_tab_relief = 2.0;
btn_tab_relief_clr = 0.2;
btn_root_z = top_underside_z + btn_tab_relief;
btn_tab_len = btn_root_z - btn_z0;
btn_relief_x = btn_tab_w / 2 + btn_tab_slit;
btn_relief_y0 = btn_in - btn_root_in - btn_tab_relief_clr;
btn_relief_y1 = btn_out + btn_root_out + btn_tab_relief_clr;
btn_tab_strain = 1.5 * btn_wall * btn_tab_flex / (btn_tab_len * btn_tab_len);

mb_clr = -0.15;
mb_d = mic_seat_d - 2 * mb_clr;
mb_cav_d = mic_d + 0.2;
mb_lip_t = 0.4;
mb_lip_in_r = mic_d / 2 - 0.6;
mb_lip_out_r = mb_cav_d / 2 + 0.2;
mb_lip_span = 50;
mb_back_gap = 0.05;
mb_min_t = 0.5;
mb_channel_d = mic_port_d + 1.2;
mb_env_half = 9;
mb_lead = 0.3;
mb_relief_clr = 0.4;
mb_relief_depth = mic_pin_tail + 0.2;
mic_pin_half = (mic_pin_n - 1) / 2 * mic_pin_pitch;

dk_hole_d = 3.0;
dk_hole_pitch = 23.2;
dk_hole_in = 2.25;
dk_hole_x = dk_hole_pitch / 2;
dk_mod_clr = 0.3;
dk_standoff_wall = dk_hole_x - insert_d / 2 - (dk_module_w / 2 + dk_mod_clr);
dk_screw_head_z = dk_z1 + csk_stand(dk_hole_d);
dk_screw_tip_z = dk_screw_head_z - screw_len;
dk_screw_engage = dk_z0 - dk_screw_tip_z;
dk_screw_thread = min(insert_len, dk_screw_engage);
dk_insert_hole_depth = max(insert_hole_depth, ceil1(dk_screw_engage + insert_tip_gap));
dk_pin_gap = (dk_l - dk_pin_l) / 2 - (dk_hole_in + screw_head_d / 2);

assert(ring_h == 3.0);
assert(ring_front_t >= 0.5 && ring_front_t <= 0.7);
assert(abs(ring_outer_r - filament_r - ring_pocket_r - ring_front_t) < 1e-9);
assert(ring_in_r >= spk_seat_max_r + ring_in_clr);
assert(ring_in_r >= spk_flange_max_r + ring_in_clr);
assert(top_skirt_in_r >= spk_flange_max_r + ring_in_clr);
assert((button_az - spk_rot) % 90 == 0);
assert(abs(button_r - btn_w / 2 - (spk_flange_half + spk_seat_flange_clr) - btn_pocket_clr) < 1e-9);
assert(abs(ring_outer_r - norm([button_r + btn_in, btn_in]) - btn_wall) < 1e-9);
assert(ring_outer_r * 2 <= 108);
assert(fil_excess >= 0 && ring_arc_len < fil_lit);
assert(ring_outer_r * 2 < bed);
assert(antenna_metal_gap >= 10);
assert(abs(norm(heel_c - bowl_c) - (bowl_rad - heel_r)) < 1e-6);
assert(bowl_rr > heel_r && bowl_rad > heel_r);
assert(plinth_h > 0 && edge_zt > plinth_h && edge_zt < mic_z - mic_d / 2 - 1);
assert(plinth_band >= 6 && plinth_band <= 8);
assert(heel_h >= 10);
assert(heel_step >= 3 && plinth_inset >= heel_step);
assert(edge_phi > 45 && edge_phi < edge_t_phi && edge_t_phi <= 90);
assert(top_edge_z0 - ring_z1 <= 4);
assert(wall_t >= lip_face_need);
assert(spk_low_z >= dk_pin_top_z + wire_clear_h - 1e-9);
assert(spk_low_z >= amp_pin_top_z + wire_clear_h);
assert(spk_low_z - 0.5 < dk_pin_top_z + wire_clear_h);
assert(total_h <= 78);
assert(cone_gap >= 3 && cone_gap <= 5);
assert(grille_open_ratio >= 0.30);
assert(grille_r + grille_hole_d / 2 < gasket_inner_r);
assert(boss_wall >= 1.6);
assert(boss_skin >= 1.6);
assert(spk_insert_top_z <= top_underside_z);
assert(foot_out >= 1.5 && foot_out <= 2.5);
assert(foot_niche_skin >= 0.8 && foot_niche_h > 1);
assert(foot_niche_clr >= 0.2 && foot_niche_clr <= 0.5);
assert(foot_r + foot_l / 2 <= plinth_r - tray_flange_lip - 0.3);
assert(foot_corner_r * 2 < min(foot_w, foot_l));
assert(dkf_latch_strain <= 0.016);
assert(dkb_latch_strain <= 0.016);
assert(amp_latch_strain <= 0.016);
assert(amp_w / 2 - amp_pad > amp_latch_dx + amp_latch_w / 2 + latch_relief_clr + 0.1);
assert(amp_w / 2 - (amp_hdr_in + amp_hdr_w + amp_hdr_pad_gap + amp_hdr_pad_w) > amp_latch_dx + amp_latch_w / 2 + latch_relief_clr + 0.1);
assert(amp_hdr_in + amp_hdr_w / 2 + amp_hdr_pin_d / 2 + 1.0 < amp_hdr_in + amp_hdr_w + amp_hdr_pad_gap);
assert(amp_w / 2 - amp_seg_len > amp_latch_dx + amp_latch_w / 2 + latch_relief_clr + 0.1);
assert(amp_hdr_n * amp_hdr_pitch <= amp_l + 0.2);
assert(amp_hdr_h + amp_hdr_pin + amp_t < amp_total_h);
assert(amp_hdr_in + amp_hdr_w < amp_w - amp_block_w - 2);
assert(amp_stop_len < (amp_l - amp_block_l) / 2 - 0.5);
assert(amp_hdr_tail < 3 - 0.5);
assert(amp_block_l <= amp_l - 2 && amp_block_w <= amp_w - 2);
assert(usb_win_z0 >= 1.5);
assert(abs(usb_win_h - usb_plug_h - 0.5) < 1e-9);
assert(abs(usb_win_w - usb_plug_w - 0.3) < 1e-9);
assert(latch_relief > 0 && tray_floor_t - latch_relief > tray_bot_z + 1.8);
assert(amp_latch_relief >= latch_relief && tray_floor_t - amp_latch_relief > tray_bot_z + 1.8);
assert(amp_clr >= clr);
assert(tray_gap >= tray_gap_min && tray_gap <= tray_gap_max);
assert(top_screw_tip_z - insert_tip_gap >= top_insert_bot_z - 1e-9);
assert(top_screw_engage >= insert_len);
assert(top_insert_bot_z > top_screw_ledge_z1);
assert(top_seat_floor_t >= 1.6);
assert(top_col_z1 < top_underside_z && top_boss_h > 0);
assert(top_plug_d >= csk_d + 1.0);
assert(top_plug_depth >= 1.0 && top_plug_depth < top_plug_d);
assert(top_plug_fit > 0 && top_plug_fit <= 0.2);
assert(top_plug_chamfer > 0 && top_plug_chamfer < top_plug_depth / 2);
assert(top_screw_r + top_plug_d / 2 + 1.0 <= top_face_r);
assert(top_screw_r + boss_od / 2 <= top_skirt_in_r - 0.5);
assert(chamfer_slope >= 1 && top_corbel_z0 > dk_pin_top_z + wire_clear_h + 5);
assert(spk_screw_tip_z + insert_tip_gap <= spk_insert_top_z + 1e-9);
assert(abs(tray_pad_t - tray_floor_t - tray_pad_gap) < 1e-9);
assert(tray_screw_thread >= min_thread);
assert(top_screw_thread >= min_thread);
assert(spk_screw_thread >= min_thread);
assert(tray_screw_tip_z + tray_pad_gap + insert_tip_gap <= tray_pad_t + tray_boss_hole_depth + 1e-9);
assert(tray_boss_hole_depth >= insert_len + 1);
assert(top_insert_hole_depth >= insert_len + 1);
assert(spk_insert_hole_depth >= insert_len + 1);
assert(tray_gasket_squeeze > 0 && tray_gasket_squeeze < tray_gasket_t);
assert(lip_t >= 0.6 && lip_t <= 0.8);
assert(lip_free_h >= 1.2 && lip_free_h <= 1.5 + 1e-9);
assert(lip_lean > 0 && lip_lean <= 45);
assert(lip_phi > 0 && lip_phi < 45);
assert(lip_hc_set - tray_gasket_squeeze - 0.2 >= 0);
assert(lip_w_h * sin(lip_phi) < lip_base_t - 0.05);
assert(lip_vlen / lip_t >= 1.5);
assert(tray_pad_gap >= tray_gasket_squeeze + 0.2 - 1e-9);
assert(tray_pad_top_z - tray_pad_relief_depth - (tray_seat_z + csk_h) >= 1.5);
assert(tray_screw_recess >= 0 && tray_seat_z + csk_h < tray_bot_z + tray_flange_t + tray_gasket_t);
assert(tray_pad_relief_d > insert_od + 1 && tray_pad_relief_d < boss_od - 2);
assert(abs(screw_rim_h - 0.4) < 1e-9 && screw_rim_h >= 0.2);
assert(csk_bore_h >= screw_rim_h);
assert(washer_id > screw_d && washer_id < screw_head_d);
assert(washer_od >= spk_hole_l + 0.5);
assert(spk_hole_r - washer_od / 2 >= spk_basket_top_d / 2 + 1.0);
assert(spk_hole_edge_dist - csk_d / 2 >= 1.2);
assert(spk_hole_edge_dist - washer_od / 2 >= 1.0);
assert(csk_stand(washer_id) > screw_rim_h && csk_stand(washer_id) < screw_head_h);
assert((boss_od - csk_d) / 2 >= 1.2);
assert(top_screw_ledge >= 3.0);
assert(top_screw_r - boss_od / 2 > spk_basket_top_d / 2 + 1);
assert(top_screw_r - boss_od / 2 - spk_flange_half >= 2.5);
assert(top_screw_r + boss_od / 2 <= ring_in_r - ring_in_clr + 1e-9);
assert(top_boss_gap > 0 && abs(top_boss_z0 - top_col_z1 - top_boss_gap) < 1e-9);
assert(len([for (a = top_screw_angles) if ((a - spk_rot) % 90 != 0) a]) == 0);
assert(top_screw_r - boss_od / 2 >= spk_flange_half + spk_seat_flange_clr + 0.5);
assert(top_screw_r + screw_head_d / 2 < filament_r - 1.5);
assert(bridge_in_r <= filament_r - ring_pocket_r - 1.0);
assert(chan_p2[1] - chan_d / 2 >= 0.5);
assert(min([for (a = top_screw_angles, b = bridge_angles) abs(spk_wrap(a - b))])
    > bridge_half_deg + 180 / PI * (boss_od / 2 + 1) / top_screw_r);
assert(wedge_out_r < ring_outer_r - 1);
assert(chan_d >= 1.6 && chan_d <= 1.8 && chan_d > fil_d);
assert(chan_probe_d >= 1.6 && chan_probe_d <= chan_d);
assert(fil_bend_r >= fil_bend_r_min + (chan_d - fil_d) - 1e-9);
assert(fil_straight_lit >= fil_trans);
assert(chan_straight >= fil_trans + fil_contact);
assert(chan_len >= fil_excess + fil_contact - 1e-9);
assert(chan_open_l > 0 && chan_open_l <= chan_bend_len);
assert(min([for (u = [0 : 0.25 : chan_open_l]) chan_pt(u)[2]]) + chan_open_up - chan_d / 2 >= bridge_top_z);
assert(fil_contact_t < fil_contact_w && fil_contact_w < chan_d);
assert(abs(norm(chan_p1 - chan_p0) - fil_bend_r * sqrt(2)) < 1e-9);
assert(abs(chan_dir * chan_t) < 1e-9);
assert(spk_rim_h > 0);
assert(btn_z0 - btn_stub_l - top_z0 >= btn_leg_gap - 1e-9);
assert(btn_stub_l < btn_leg_l);
assert(button_r - btn_leg_x + btn_leg_t / 2 <= ring_in_r - ring_in_clr);
assert(btn_hook_z0 > top_z0);
assert(button_r - btn_leg_x - btn_leg_t / 2 - spk_flange_half >= 1.0);
assert(button_r + button_hole_d / 2 < top_face_r);
assert(top_outer_r(H - btn_collar_h) >= button_r + btn_collar_r);
assert(btn_rim_min >= 0.8);
assert(norm([button_r + btn_tab_w / 2, btn_out + btn_tab_flex]) < top_skirt_in_r);
assert(btn_tip_gap >= 0.5);
assert(top_edge_z0 > top_z0 + 1);
assert(top_face_r > dimple_r + dimple_d / 2 + 1);
assert(top_skirt_in_r < ring_outer_r - 3);
assert(top_plate_t - dimple_depth >= 1.5);
assert(spk_hole_r - boss_od / 2 > gasket_inner_r);
assert(spk_hole_edge_dist + spk_seat_flange_clr - boss_od / 2 >= 1.0);
assert(spk_slot_edge_clr >= 1.5);
assert(spk_hole_l > spk_hole_d && spk_hole_d >= screw_thru_d);
assert(spk_seat_side_w >= 1.0 && spk_seat_gap >= 0.5);
assert(spk_surround_d / 2 + spk_seat_gap <= gasket_inner_r);
assert(spk_hole_r - spk_hole_l / 2 - spk_basket_top_d / 2 >= 1.0);
assert(spk_flange_half - spk_basket_top_d / 2 >= 1.0);
assert(len(spk_term_az) == 1);
assert(spk_flange_fillet_r >= 0);
assert(spk_flange_arc_phi > 0 && spk_flange_arc_phi < 45);
assert(abs(flg_tip(spk_side_arc_r) - spk_flange_max_r) < 1e-6);
assert(abs(spk_side_arc_cx + spk_side_arc_r * cos(spk_flange_arc_phi) - spk_flange_corner_w / 2) < 1e-6);
assert(abs(spk_side_arc_cx + spk_side_arc_r - spk_flange_half) < 1e-9);
assert(abs(norm([spk_flange_fillet_c - spk_side_arc_cx, spk_flange_fillet_c]) - (spk_side_arc_r - spk_flange_fillet_r)) < 1e-6);
assert(abs(spk_flange_fillet_c * sqrt(2) + spk_flange_fillet_r - spk_flange_max_r) < 1e-9);
assert(spk_seat_flange_clr >= 0.75);
assert(spk_term_in > 0 && spk_term_out >= 0 && spk_flange_half + spk_term_out <= 39 + 1e-9);
assert(norm([dimple_keep - insert_d / 2, H - dimple_depth - spk_insert_top_z]) >= 1.6);
assert(btn_bore_d >= btn_flange_d + 0.4);
assert(btn_bore_d / 2 < btn_in);
assert(btn_tip_z - btn_tip_gap > btn_ceil_z);
assert(btn_w / 2 - btn_hook_ov >= btn_flange_d / 2 + 0.15);
assert(btn_hook_ov >= 0.6 && btn_hook_ov <= 0.8);
assert(top_plate_t - btn_tab_relief >= 1.0);
assert(btn_relief_y0 - button_hole_d / 2 >= 0.8);
assert(norm([button_r + btn_relief_x, btn_relief_y1]) < top_outer_r(btn_root_z) - 1.0);
assert(btn_hook_ov >= 0.3 && btn_hook_u > btn_hook_ov);
assert(btn_tab_flex >= btn_hook_ov + 0.1 - 1e-9);
assert(btn_tab_strain <= 0.016);
assert(btn_in - btn_root_in > btn_bore_d / 2);
assert(btn_in - btn_root_in >= btn_flange_d / 2 + 0.1);
assert(btn_tab_slit >= 0.5);
assert(btn_tab_w / 2 + btn_tab_slit < btn_in - 1);
assert(btn_hook_z0 > btn_z0 - 1);
assert(btn_bore_d / 2 >= btn_pusher_d / 2 + 1);
assert(top_skirt_in_r >= arc_top_in_r + 0.3);
assert(let (bx = button_r * cos(button_az), by = button_r * sin(button_az))
    min([for (a = spk_hole_angles)
        norm([bx - spk_hole_r * cos(a), by - spk_hole_r * sin(a)])])
    - boss_od / 2 - norm([max(btn_out, btn_skirt_x), btn_out]) > 1.0);
assert(mb_d > mic_seat_d && mb_d <= mic_seat_d + 0.5);
assert(mb_cav_d > mic_d && mb_cav_d < mb_d - 0.8);
assert(mb_lip_in_r < mic_d / 2 - 0.3 && mb_lip_out_r <= mb_d / 2 - 0.2);
assert(mb_lip_t >= 0.4 && mb_min_t >= 0.4);
assert(mb_channel_d > mic_port_d);
assert(mic_boot_t + mic_seat_depth - mb_back_gap >= mb_min_t + 0.5);
assert(mb_env_half >= mic_d / 2 + 1);
assert(mb_lead >= -2 * mb_clr && mb_lead < 0.6);
assert(btn_stem_sq * sqrt(2) / 2 + clr * sqrt(2) + 0.4 <= (button_hole_d - 2 * clr) / 2);
assert(btn_leg_x + btn_leg_t / 2 < btn_w / 2);
assert(btn_leg_y + btn_leg_w / 2 < btn_w / 2 - 1);
assert(norm([mic_pin_row, mic_pin_half]) + mic_pin_d / 2 + mb_relief_clr <= mb_cav_d / 2 - 0.4);
assert(norm([mic_pin_row, mic_pin_half]) + mic_pin_d / 2 <= mb_lip_in_r - 0.2);
assert(mic_pin_row - mic_pin_d / 2 - mb_relief_clr - mb_channel_d / 2 >= 0.8);
assert(mic_pin_row + mic_pin_d / 2 < mic_d / 2 - 1.0);
assert(mic_can_w / 2 + 0.5 < mic_pin_row - mic_pin_d / 2);
assert(mic_can_h + mic_t - mic_total_t > -1e-9 && mic_can_h + mic_t - mic_total_t < 1e-9);
assert(dk_hole_d >= screw_d + 0.3);
assert(dk_hole_x + dk_hole_d / 2 < dk_w / 2 && dk_hole_in > dk_hole_d / 2);
assert(dk_screw_thread >= min_thread);
assert(dk_screw_tip_z - insert_tip_gap >= dk_z0 - dk_insert_hole_depth - 1e-9);
assert(dk_insert_hole_depth >= insert_len + 1);
assert(dk_z0 - dk_insert_hole_depth > tray_bot_z + 1.5);
assert(csk_stand(dk_hole_d) > screw_rim_h && csk_stand(dk_hole_d) < screw_head_h);
assert(dk_standoff_wall >= 0.6);
assert(dk_hole_x - dk_hole_d / 2 > dk_module_w / 2 + dk_mod_clr);
assert(dk_pin_gap >= 0.5);
assert(dk_hole_x - screw_head_d / 2 > dkf_latch_w / 2 + 0.5);
