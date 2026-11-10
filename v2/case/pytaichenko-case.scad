include <params.scad>
use <lib.scad>
use <dummies.scad>
use <top.scad>
use <body.scad>
use <ring.scad>
use <tray.scad>
use <small.scad>

part = "assembly";
lid_alpha = 0.35;

module echo_params() {
    echo(clr=clr, bed=bed, board_clr=board_clr, case_d=case_d, case_r=case_r, wall_t=wall_t, wall_thin_t=wall_thin_t, inner_r=inner_r, part_r=part_r);
    echo(insert_d=insert_d, insert_len=insert_len, insert_od=insert_od, insert_hole_depth=insert_hole_depth, boss_od=boss_od, boss_wall=boss_wall, insert_tip_gap=insert_tip_gap, min_thread=min_thread);
    echo(screw_len=screw_len, screw_d=screw_d, screw_thru_d=screw_thru_d, screw_head_d=screw_head_d, screw_head_h=screw_head_h, screw_rim_h=screw_rim_h);
    echo(csk_clr=csk_clr, csk_gap=csk_gap, csk_d=csk_d, csk_bore_h=csk_bore_h, csk_h=csk_h, csk_head_z=csk_head_z);
    echo(latch_k=latch_k, latch_vg=latch_vg, latch_relief=latch_relief, latch_root_fillet=latch_root_fillet, latch_relief_clr=latch_relief_clr, latch_side_clr=latch_side_clr);
    echo(tray_skin_t=tray_skin_t, tray_floor_t=tray_floor_t, wire_clear_h=wire_clear_h);
    echo(dk_len=dk_len, dk_ant_out=dk_ant_out, dk_ant_w=dk_ant_w, dk_ant_t=dk_ant_t, dk_board_len=dk_board_len, dk_w=dk_w, dk_t=dk_t);
    echo(dk_row_pitch=dk_row_pitch, dk_row_x=dk_row_x, dk_pins=dk_pins, dk_pin_pitch=dk_pin_pitch, dk_pin1_from_usb=dk_pin1_from_usb, dk_pin_sq=dk_pin_sq);
    echo(dk_hdr_w=dk_hdr_w, dk_hdr_h=dk_hdr_h, dk_pin_short=dk_pin_short, dk_pin_long=dk_pin_long, dk_nest_wall=dk_nest_wall);
    echo(dk_usb_w=dk_usb_w, dk_usb_h=dk_usb_h, dk_usb_top=dk_usb_top, dk_usb_pitch=dk_usb_pitch, dk_usb_out=dk_usb_out, dk_usb_len=dk_usb_len);
    echo(dk_com_side=dk_com_side, dk_com_x=dk_com_x, dk_usb2_x=dk_usb2_x, dk_mod_w=dk_mod_w, dk_mod_l=dk_mod_l, dk_mod_h=dk_mod_h);
    echo(dk_hdr_z0=dk_hdr_z0, dk_z0=dk_z0, dk_z1=dk_z1, dk_pin_top_z=dk_pin_top_z, dk_pin_bot_z=dk_pin_bot_z);
    echo(dk_y0=dk_y0, dk_y1=dk_y1, dk_ant_y1=dk_ant_y1, dk_pin_y0=dk_pin_y0, dk_pin_y1=dk_pin_y1, antenna_top_z=antenna_top_z);
    echo(usb_face_y=usb_face_y, usb_face_gap=usb_face_gap, usb_face_gap_max=usb_face_gap_max, usb_z=usb_z);
    echo(usb_plug_w=usb_plug_w, usb_plug_h=usb_plug_h, usb_plug_len=usb_plug_len, usb_plug_seat=usb_plug_seat, usb_plug_face_y=usb_plug_face_y);
    echo(usb_shell_w=usb_shell_w, usb_shell_h=usb_shell_h, usb_shell_l=usb_shell_l, usb_shell_tip_y=usb_shell_tip_y, usb_cav_clr=usb_cav_clr);
    echo(spk_flange_d=spk_flange_d, spk_flange_t=spk_flange_t, spk_open_d=spk_open_d, spk_rim_w=spk_rim_w, spk_rim_h=spk_rim_h);
    echo(spk_ear_tip=spk_ear_tip, spk_ear_pitch=spk_ear_pitch, spk_ear_hole_d=spk_ear_hole_d, spk_ear_tip_r=spk_ear_tip_r, spk_ear_base_x=spk_ear_base_x, spk_ear_base_w=spk_ear_base_w);
    echo(spk_depth=spk_depth, spk_basket_d=spk_basket_d, spk_magnet_d=spk_magnet_d, spk_magnet_h=spk_magnet_h, spk_cap_rise=spk_cap_rise, spk_grille_gap=spk_grille_gap);
    echo(spk_term_z=spk_term_z, spk_term_out=spk_term_out, spk_term_in=spk_term_in, spk_term_w=spk_term_w, spk_term_h=spk_term_h, spk_term_tip=spk_term_tip, spk_term_z0=spk_term_z0, spk_term_z1=spk_term_z1);
    echo(spk_gasket_t=spk_gasket_t, spk_gasket_squeeze=spk_gasket_squeeze, spk_gasket_c=spk_gasket_c, spk_ear_standoff_h=spk_ear_standoff_h, spk_mag_gap=spk_mag_gap, grille_t=grille_t);
    echo(spk_mag_z=spk_mag_z, spk_magnet_top_z=spk_magnet_top_z, spk_rim_top_z=spk_rim_top_z, spk_ear_z=spk_ear_z, spk_flange_bot_z=spk_flange_bot_z, spk_cap_top_z=spk_cap_top_z);
    echo(spk_lid_z=spk_lid_z, grille_bot_z=grille_bot_z, H=H, antenna_metal_gap=antenna_metal_gap);
    echo(amp_w=amp_w, amp_l=amp_l, amp_t=amp_t, amp_total_h=amp_total_h, amp_block_h=amp_block_h, amp_block_w=amp_block_w, amp_block_l=amp_block_l);
    echo(amp_hdr_n=amp_hdr_n, amp_hdr_pitch=amp_hdr_pitch, amp_hdr_w=amp_hdr_w, amp_hdr_h=amp_hdr_h, amp_hdr_pin=amp_hdr_pin, amp_hdr_pin_d=amp_hdr_pin_d, amp_hdr_in=amp_hdr_in, amp_hdr_tail=amp_hdr_tail);
    echo(amp_clr=amp_clr, amp_pad_h=amp_pad_h, amp_z0=amp_z0, amp_x0=amp_x0, amp_xy=amp_xy, amp_rot=amp_rot, amp_y=amp_y, amp_hdr_lx=amp_hdr_lx, amp_hdr_xy=amp_hdr_xy);
    echo(amp_pin_top_z=amp_pin_top_z, amp_block_top_z=amp_block_top_z, amp_top_z=amp_top_z);
    echo(cap_d=cap_d, cap_l=cap_l, cap_gap=cap_gap, cap_xy=cap_xy, cap_axis_y=cap_axis_y, cap_axis_z=cap_axis_z);
    echo(mic_d=mic_d, mic_t=mic_t, mic_total_t=mic_total_t, mic_can_h=mic_can_h, mic_can_w=mic_can_w, mic_can_l=mic_can_l, mic_port_d=mic_port_d);
    echo(mic_angle=mic_angle, mic_z=mic_z, mic_az_r=mic_az_r, mic_az_l=mic_az_l, mic_face_r=mic_face_r, mic_board_r=mic_board_r, mic_hole_chord=mic_hole_chord, mic_board_chord=mic_board_chord);
    echo(btn_w=btn_w, btn_body_h=btn_body_h, btn_h=btn_h, btn_pusher_d=btn_pusher_d, btn_tip_gap=btn_tip_gap, button_hole_d=button_hole_d, top_plate_t=top_plate_t);
    echo(btn_leg_x=btn_leg_x, btn_leg_y=btn_leg_y, btn_leg_w=btn_leg_w, btn_leg_t=btn_leg_t, btn_leg_l=btn_leg_l, btn_stub_l=btn_stub_l, btn_leg_gap=btn_leg_gap);
    echo(btn_pocket_clr=btn_pocket_clr, btn_wall=btn_wall, btn_in=btn_in, btn_out=btn_out, btn_flange_gap=btn_flange_gap, button_az=button_az, button_r=button_r, btn_corner_r=btn_corner_r);
    echo(btn_tip_z=btn_tip_z, btn_z0=btn_z0, top_region_h=top_region_h, top_z0=top_z0);
    echo(ring_h=ring_h, ring_front_t=ring_front_t, ring_pocket_r=ring_pocket_r, ring_z0=ring_z0, ring_z1=ring_z1);
    echo(fil_lit=fil_lit, fil_contact=fil_contact, fil_d=fil_d, fil_bend_r_min=fil_bend_r_min, filament_r=filament_r, fil_z=fil_z);
    echo(fil_circ=fil_circ, fil_gap_len=fil_gap_len, fil_lit_deg=fil_lit_deg, btn_pocket_arc=btn_pocket_arc);
    echo(tr_w=tr_w, tr_body_l=tr_body_l, tr_body_h=tr_body_h, tr_tab_l=tr_tab_l, tr_tab_t=tr_tab_t, tr_lead_l=tr_lead_l, tr_lead_w=tr_lead_w, tr_lead_t=tr_lead_t, tr_lead_z=tr_lead_z, tr_lead_pitch=tr_lead_pitch);
    echo(tr_len=tr_len, tr_zone=tr_zone, transistor_xy=transistor_xy, tr_z0=tr_z0);
    echo(washer_id=washer_id, washer_od=washer_od, washer_t=washer_t, spk_ear_xy=spk_ear_xy, spk_standoff_z0=spk_standoff_z0, spk_washer_bot_z=spk_washer_bot_z);
    echo(spk_screw_head_z=spk_screw_head_z, spk_screw_tip_z=spk_screw_tip_z, spk_screw_engage=spk_screw_engage, spk_screw_thread=spk_screw_thread, spk_insert_hole_depth=spk_insert_hole_depth, spk_insert_top_z=spk_insert_top_z, spk_boss_skin=spk_boss_skin);
    echo(spk_gasket_id=spk_gasket_id, spk_gasket_od=spk_gasket_od, spk_gasket_z0=spk_gasket_z0, spk_recess_r=spk_recess_r, spk_lip_in_r=spk_lip_in_r, spk_lip_w=spk_lip_w, spk_lip_drop=spk_lip_drop, spk_lip_z0=spk_lip_z0);
    echo(top_under_z=top_under_z, top_t=top_t, top_skirt_in_r=top_skirt_in_r, top_edge_r=top_edge_r, top_edge_psi=top_edge_psi, top_bevel_l=top_bevel_l, top_edge_z0=top_edge_z0, top_face_r=top_face_r);
    echo(face_thru_r=face_thru_r, face_min_hole_d=face_min_hole_d, face_min_web=face_min_web, face_edge_land=face_edge_land);
    echo(sf_golden=sf_golden, sf_spin=sf_spin, sf_dimple_depth=sf_dimple_depth, sf_dimple_wall=sf_dimple_wall, sf_dimple_floor_shrink=sf_dimple_floor_shrink, sf_dimple_floor_max=sf_dimple_floor_max, sf_plug_clear=sf_plug_clear, sf_btn_clear=sf_btn_clear, sf_relief_clear=sf_relief_clear);
    echo(sf_c=sf_c, sf_n0=sf_n0, sf_d=sf_d, sf_thru_shrink=sf_thru_shrink, sf_dimple_d0=sf_dimple_d0, sf_dimple_d1=sf_dimple_d1, sf_thru_n=sf_thru_n, sf_dimple_n=sf_dimple_n, sf_min_web=sf_min_web, sf_thru_reach=sf_thru_reach, sf_open_ratio=sf_open_ratio);
    echo(sf_pts=sf_pts);
    echo(top_screw_r=top_screw_r, top_screw_angles=top_screw_angles, top_screw_xy=top_screw_xy, top_plug_d=top_plug_d, top_plug_depth=top_plug_depth, top_plug_h=top_plug_h, top_plug_fit=top_plug_fit, top_plug_clr=top_plug_clr, top_plug_ribs=top_plug_ribs, top_plug_rib_w=top_plug_rib_w, top_plug_edge=top_plug_edge, top_plug_grip_h=top_plug_grip_h, top_plug_chamfer=top_plug_chamfer, top_plug_rim=top_plug_rim);
    echo(top_seat_z=top_seat_z, top_screw_head_z=top_screw_head_z, top_screw_tip_z=top_screw_tip_z, top_seat_floor_t=top_seat_floor_t, top_boss_z0=top_boss_z0, top_boss_gap=top_boss_gap, top_col_z1=top_col_z1, top_sock_d=top_sock_d);
    echo(top_screw_engage=top_screw_engage, top_screw_thread=top_screw_thread, top_insert_hole_depth=top_insert_hole_depth, top_insert_bot_z=top_insert_bot_z);
    echo(btn_plate_z=btn_plate_z, btn_ceil_gap=btn_ceil_gap, btn_ceil_z=btn_ceil_z, btn_bore_d=btn_bore_d, btn_flange_d=btn_flange_d, btn_rim_min=btn_rim_min);
    echo(btn_hook_ov=btn_hook_ov, btn_hook_slope=btn_hook_slope, btn_hook_u=btn_hook_u, btn_hook_z0=btn_hook_z0, btn_tab_w=btn_tab_w, btn_tab_slit=btn_tab_slit, btn_tab_cut_in=btn_tab_cut_in);
    echo(btn_root_in=btn_root_in, btn_root_out=btn_root_out, btn_tab_t=btn_tab_t, btn_tab_flex=btn_tab_flex, btn_tab_relief=btn_tab_relief, btn_tab_relief_clr=btn_tab_relief_clr, btn_root_z=btn_root_z, btn_tab_len=btn_tab_len);
    echo(btn_relief_x=btn_relief_x, btn_relief_y0=btn_relief_y0, btn_relief_y1=btn_relief_y1, btn_relief_z0=btn_relief_z0, btn_tab_strain=btn_tab_strain, btn_skirt_x=btn_skirt_x);
    echo(body_bot_z=body_bot_z, body_layer=body_layer, first_layer=first_layer, ring_arc_h=ring_arc_h, chamfer_slope=chamfer_slope);
    echo(fil_trans=fil_trans, fil_bend_r=fil_bend_r, fil_turn=fil_turn, fil_bend_steps=fil_bend_steps, chan_d=chan_d, fil_bend_len=fil_bend_len, fil_tail_lit=fil_tail_lit, fil_straight=fil_straight);
    echo(fil_ring_len=fil_ring_len, fil_ring_deg=fil_ring_deg, fil_ring_half=fil_ring_half, fil_end_az=fil_end_az, fil_col_clr=fil_col_clr);
    echo(fil_tail_pts=fil_tail_pts, fil_tail_pts_m=fil_tail_pts_m, fil_tail_end=fil_tail_end, fil_contact_start=fil_contact_start);
    echo(ring_lip_h=ring_lip_h, ring_lip_w=ring_lip_w, ring_lip_clr=ring_lip_clr, ring_lip_chamfer=ring_lip_chamfer, ring_in_clr=ring_in_clr, ring_end_gap=ring_end_gap, ring_neck_w=ring_neck_w);
    echo(ring_outer_r=ring_outer_r, ring_lip_out_r=ring_lip_out_r, ring_in_r=ring_in_r, ring_end_deg=ring_end_deg, ring_slit_in=ring_slit_in, ring_slit_out=ring_slit_out, ring_axis_h=ring_axis_h);
    echo(ring_front_wall=ring_front_wall, ring_above_t=ring_above_t, ring_below_t=ring_below_t, ring_top_z=ring_top_z, bridge_a0=bridge_a0, bridge_a1=bridge_a1);
    echo(top_ledge_h=top_ledge_h, top_ledge_z0=top_ledge_z0, top_ledge_z1=top_ledge_z1, col_bar_w=col_bar_w, col_wedge_r=col_wedge_r, col_corbel_z0=col_corbel_z0);
    echo(tray_screw_r=tray_screw_r, tray_screw_angles=tray_screw_angles, tray_screw_xy=tray_screw_xy, tray_insert_hole_depth=tray_insert_hole_depth, tray_insert_top_z=tray_insert_top_z, tray_tip_h=tray_tip_h, tray_boss_skin=tray_boss_skin, tray_boss_top_z=tray_boss_top_z);
    echo(mic_pocket_clr=mic_pocket_clr, mic_pocket_r=mic_pocket_r, mic_rim_w=mic_rim_w, mic_fill_r=mic_fill_r, mic_open_r=mic_open_r, mic_recess=mic_recess, mic_port_out_d=mic_port_out_d, mic_port_flare=mic_port_flare);
    echo(top_gasket_t=top_gasket_t, top_gasket_squeeze=top_gasket_squeeze, top_gasket_c=top_gasket_c, top_gasket_w=top_gasket_w, top_gasket_fit=top_gasket_fit, top_groove_wall=top_groove_wall, top_groove_out_r=top_groove_out_r, top_groove_in_r=top_groove_in_r, top_groove_depth=top_groove_depth, top_gasket_od=top_gasket_od, top_gasket_id=top_gasket_id);
    echo(wire_probe_d=wire_probe_d, wire_probe_r=wire_probe_r, wire_probe_z1=wire_probe_z1, wire_btn_pts=wire_btn_pts, wire_term_pts=wire_term_pts);
    echo(tray_slot_w=tray_slot_w, tray_slot_y0=tray_slot_y0, tray_slot_y1=tray_slot_y1, tray_pin_gap=tray_pin_gap, tray_pin_floor_z=tray_pin_floor_z, tray_pin_groove_w=tray_pin_groove_w, tray_pin_groove_y0=tray_pin_groove_y0, tray_pin_groove_y1=tray_pin_groove_y1);
    echo(tray_rail_in_x=tray_rail_in_x, tray_rail_out_x=tray_rail_out_x, tray_rail_top_z=tray_rail_top_z, tray_rail_y1=tray_rail_y1, tray_latch_relief=tray_latch_relief, tray_latch_root_z=tray_latch_root_z);
    echo(dks_latch_t=dks_latch_t, dks_latch_ov=dks_latch_ov, dks_latch_c=dks_latch_c, dks_latch_w=dks_latch_w, dks_latch_y=dks_latch_y, dks_latch_strain=dks_latch_strain);
    echo(dkf_latch_t=dkf_latch_t, dkf_rail_cut=dkf_rail_cut, dkf_latch_ov=dkf_latch_ov, dkf_latch_c=dkf_latch_c, dkf_latch_x0=dkf_latch_x0, dkf_latch_x1=dkf_latch_x1, dkf_latch_strain=dkf_latch_strain);
    echo(tray_amp_pad_lx1=tray_amp_pad_lx1, tray_amp_bar_lx0=tray_amp_bar_lx0, tray_amp_stop_t=tray_amp_stop_t, tray_amp_stop_top_z=tray_amp_stop_top_z, tray_com_w=tray_com_w, tray_com_h=tray_com_h, tray_com_web=tray_com_web);
    echo(amp_latch_t=amp_latch_t, amp_latch_ov=amp_latch_ov, amp_latch_c=amp_latch_c, amp_latch_w=amp_latch_w, amp_latch_lx=amp_latch_lx, amp_lip_w=amp_lip_w, amp_lip_lx=amp_lip_lx, amp_lip_gap=amp_lip_gap, amp_lip_ov=amp_lip_ov, amp_lip_z=amp_lip_z, amp_latch_strain=amp_latch_strain);
    echo(tray_screw_tip_z=tray_screw_tip_z, tray_screw_head_z=tray_screw_head_z, tray_seat_z=tray_seat_z, tray_screw_engage=tray_screw_engage, tray_screw_thread=tray_screw_thread, tray_access_d=tray_access_d, tray_col_wall=tray_col_wall, tray_col_od=tray_col_od, tray_col_drop=tray_col_drop, tray_access_z1=tray_access_z1);
    echo(tray_gasket_t=tray_gasket_t, tray_gasket_squeeze=tray_gasket_squeeze, tray_gasket_c=tray_gasket_c, tray_gasket_w=tray_gasket_w, tray_gasket_fit=tray_gasket_fit, tray_groove_wall=tray_groove_wall, tray_groove_out_r=tray_groove_out_r, tray_groove_in_r=tray_groove_in_r, tray_groove_depth=tray_groove_depth, tray_gasket_od=tray_gasket_od, tray_gasket_id=tray_gasket_id);
    echo(foot_w=foot_w, foot_l=foot_l, foot_corner_r=foot_corner_r, foot_niche_skin=foot_niche_skin, foot_niche_h=foot_niche_h, foot_out=foot_out, foot_h=foot_h, foot_niche_clr=foot_niche_clr, foot_pad_grow=foot_pad_grow, foot_pad_h=foot_pad_h, foot_r=foot_r);
    echo(foot_xy=foot_xy, foot_rot=foot_rot, tray_wire_end_y=tray_wire_end_y, tray_wire_pts=tray_wire_pts);
    echo(com_plug_w=com_plug_w, com_plug_h=com_plug_h, com_plug_clr=com_plug_clr, com_bush_fit=com_bush_fit, com_bush_in=com_bush_in, com_bush_min_wall=com_bush_min_wall, com_hole_w=com_hole_w, com_hole_h=com_hole_h, com_bush_y1=com_bush_y1, com_slot_far_y=com_slot_far_y);
    echo_small();
}

module assembly() {
    color("dimgray") dummy_speaker();
    color("seagreen") dummy_devkit();
    color("khaki") dummy_antenna();
    color("silver") dummy_usb_plug(com_plug_w, com_plug_h);
    color("royalblue") dummy_amp();
    color("navy") dummy_cap();
    color("orange") dummy_mic(mic_az_l);
    color("orange") dummy_mic(mic_az_r);
    color("red") dummy_button();
    color("white") dummy_filament();
    color("white") dummy_filament_tails();
    color("purple") dummy_transistor();
    color("gainsboro") dummy_speaker_screws();
    color("gainsboro") dummy_metal_washers();
    color("gold") dummy_inserts();
    color("gainsboro") dummy_top_screws();
    color("dimgray") body();
    color("white") ring_arc();
    color("black") dummy_top_gasket();
    color("black") speaker_gasket();
    color("dimgray", lid_alpha) top();
    color("black") top_plugs();
    color("dimgray") button_cap();
    color("dimgray") tray();
    color("black") com_bushing();
    color("black") dummy_tray_gasket();
    color("gainsboro") dummy_tray_screws();
    color("black") dummy_feet();
}

if (part == "params") echo_params();
else if (part == "asm_speaker") dummy_speaker();
else if (part == "asm_devkit") dummy_devkit();
else if (part == "asm_antenna") dummy_antenna();
else if (part == "asm_usb_plug") dummy_usb_plug();
else if (part == "asm_com_plug") dummy_usb_plug(com_plug_w, com_plug_h);
else if (part == "asm_amp") dummy_amp();
else if (part == "asm_cap") dummy_cap();
else if (part == "asm_mic_l") dummy_mic(mic_az_l);
else if (part == "asm_mic_r") dummy_mic(mic_az_r);
else if (part == "asm_button") dummy_button();
else if (part == "asm_filament") dummy_filament();
else if (part == "asm_transistor") dummy_transistor();
else if (part == "asm_speaker_screws") dummy_speaker_screws();
else if (part == "asm_metal_washers") dummy_metal_washers();
else if (part == "asm_top_screws") dummy_top_screws();
else if (part == "asm_inserts") dummy_inserts();
else if (part == "asm_spk_inserts") dummy_spk_inserts();
else if (part == "asm_body_inserts") { dummy_column_inserts(); dummy_tray_inserts(); }
else if (part == "asm_filament_tails") dummy_filament_tails();
else if (part == "asm_top_gasket") dummy_top_gasket();
else if (part == "asm_tail_drop_probe") probe_tail_drop();
else if (part == "asm_wire_probe") probe_wires();
else if (part == "asm_mic_seal_probe_l") probe_mic_seal(mic_az_l);
else if (part == "asm_mic_seal_probe_r") probe_mic_seal(mic_az_r);
else if (part == "asm_mic_port_probe_l") probe_mic_port(mic_az_l);
else if (part == "asm_mic_port_probe_r") probe_mic_port(mic_az_r);
else if (part == "body") print_body();
else if (part == "asm_body") body();
else if (part == "ring_arc") print_ring_arc();
else if (part == "asm_ring_arc") ring_arc();
else if (part == "asm_tray_screws") dummy_tray_screws();
else if (part == "asm_tray_gasket") dummy_tray_gasket();
else if (part == "asm_feet") dummy_feet();
else if (part == "asm_tray_screw_probe") probe_tray_screws();
else if (part == "asm_tray_wire_probe") probe_tray_wires();
else if (part == "tray") print_tray();
else if (part == "asm_tray") tray();
else if (part == "top") print_top();
else if (part == "asm_top") top();
else if (part == "asm_top_flexed") top(flex = true);
else if (part == "button_cap") print_button_cap();
else if (part == "asm_button_cap") button_cap();
else if (part == "speaker_gasket") print_speaker_gasket();
else if (part == "asm_speaker_gasket") speaker_gasket();
else if (part == "top_gasket") print_top_gasket();
else if (part == "tray_gasket") print_tray_gasket();
else if (part == "top_plug") print_top_plug();
else if (part == "asm_top_plugs") top_plugs();
else if (part == "foot") foot();
else if (part == "com_bushing") print_com_bushing();
else if (part == "asm_com_bushing") com_bushing();
else if (part == "assembly") assembly();
else assert(false, str("unknown part: ", part));
