#pragma once
#include "cod4_facial_retarget.hpp"

namespace cod4::face
{
    struct control_calibration { std::vector<correspondence> mapping; float translation_scale{}; };
    inline control_calibration calibrate_controls(const skeleton& source, const skeleton& target)
    {
        const auto find = [](const skeleton& bones, std::string_view name)
        {
            const auto found = std::find_if(bones.begin(), bones.end(), [&](const bone& b) { return b.name == name; });
            return found == bones.end() ? -1 : static_cast<int>(found - bones.begin());
        };
        const auto original = rest_model_space(source), destination = rest_model_space(target);
        const auto distance = [](const std::vector<pose>& poses, int a, int b)
        {
            if (a < 0 || b < 0) return 0.0f;
            float sum{};
            for (unsigned i = 0; i < 3; ++i)
            {
                const float difference = poses[a].translation[i] - poses[b].translation[i];
                sum += difference * difference;
            }
            return std::sqrt(sum);
        };
        const auto ratio = [&](std::string_view sl, std::string_view sr, std::string_view tl, std::string_view tr, float fallback)
        {
            const float from = distance(original, find(source, sl), find(source, sr));
            const float to = distance(destination, find(target, tl), find(target, tr));
            return from > 1e-4f && to > 1e-4f ? to / from : fallback;
        };
        control_calibration result;
        result.translation_scale = ratio("j_eyeball_le", "j_eyeball_ri", "jnt_eyeball_l", "jnt_eyeball_r", 0);
        if (!std::isfinite(result.translation_scale) || result.translation_scale <= 0 || result.translation_scale > 4)
            throw std::runtime_error("Invalid native facial proportions");
        // Feature sizes belong to each target head; eye spacing alone cannot
        // describe a narrow mouth, broad cheeks or a different jaw lever arm.
        const float brows = ratio("j_brow_le", "j_brow_ri", "jnt_brow_ol", "jnt_brow_or", result.translation_scale);
        const float cheeks = ratio("j_cheek_le", "j_cheek_ri", "jnt_cheeks_ml", "jnt_cheeks_mr", result.translation_scale);
        const float lips = ratio("j_lip_top_le", "j_lip_top_ri", "jnt_lips_tl", "jnt_lips_tr", result.translation_scale);
        const float corners = ratio("j_mouth_le", "j_mouth_ri", "jnt_mouth_l", "jnt_mouth_r", result.translation_scale);
        const auto add = [&](std::string_view from, std::string_view to, region area, float scale, float influence = 1,
            bool jaw_relative = false, std::string_view second = {})
        {
            const int s = find(source, from);
            if (s < 0) return; // Masked CoD4 heads have no authored mouth curves.
            const int t = find(target, to), other = second.empty() ? -1 : find(source, second);
            if (t < 0 || (!second.empty() && other < 0)) throw std::runtime_error("Native facial region control missing");
            const int source_reference = jaw_relative ? find(source, "j_jaw") : -1;
            const int target_reference = jaw_relative ? find(target, "jnt_mandible") : -1;
            if (jaw_relative && (source_reference < 0 || target_reference < 0))
                throw std::runtime_error("Native facial jaw reference missing");
            result.mapping.push_back({static_cast<unsigned>(s), static_cast<unsigned>(t), source_reference, target_reference,
                other, area, scale / result.translation_scale * influence, influence});
        };
        add("j_jaw", "jnt_mandible", region::jaw, result.translation_scale);
        add("j_eyeball_le", "jnt_eyeball_l", region::eyes, result.translation_scale);
        add("j_eyeball_ri", "jnt_eyeball_r", region::eyes, result.translation_scale);
        add("j_eye_lid_top_le", "jnt_eyelid_tl", region::eyelids, result.translation_scale);
        add("j_eye_lid_top_ri", "jnt_eyelid_tr", region::eyelids, result.translation_scale);
        add("j_eye_lid_bot_le", "jnt_eyelid_bl", region::eyelids, result.translation_scale);
        add("j_eye_lid_bot_ri", "jnt_eyelid_br", region::eyelids, result.translation_scale);
        // Eyelid child controls already inherit their parent's motion. Do not
        // drive them a second time. Brows/cheeks are siblings and need coverage.
        for (const auto& [from, to] : std::array<std::pair<std::string_view, std::string_view>, 4>{{
            {"j_brow_le", "jnt_brow_ol"}, {"j_brow_ri", "jnt_brow_or"},
            {"j_brow_le", "jnt_brow_il"}, {"j_brow_ri", "jnt_brow_ir"}}}) add(from, to, region::brows, brows);
        add("j_brow_le", "jnt_forehead_l", region::brows, brows, 0.35f);
        add("j_brow_ri", "jnt_forehead_r", region::brows, brows, 0.35f);
        for (const auto& [from, to] : std::array<std::pair<std::string_view, std::string_view>, 4>{{
            {"j_cheek_le", "jnt_cheeks_ml"}, {"j_cheek_ri", "jnt_cheeks_mr"},
            {"j_cheek_le", "jnt_cheeks_ul"}, {"j_cheek_ri", "jnt_cheeks_ur"}}}) add(from, to, region::cheeks, cheeks);
        add("j_cheek_le", "jnt_cheeks_mlo", region::cheeks, cheeks, 0.5f);
        add("j_cheek_ri", "jnt_cheeks_mro", region::cheeks, cheeks, 0.5f);
        add("j_cheek_le", "jnt_cheeks_ulo", region::cheeks, cheeks, 0.5f);
        add("j_cheek_ri", "jnt_cheeks_uro", region::cheeks, cheeks, 0.5f);
        add("j_levator_le", "jnt_levator_l", region::lips, lips);
        add("j_levator_ri", "jnt_levator_r", region::lips, lips);
        add("j_mouth_le", "jnt_mouth_l", region::lips, corners);
        add("j_mouth_ri", "jnt_mouth_r", region::lips, corners);
        add("j_mouth_le", "jnt_lips_l", region::lips, corners);
        add("j_mouth_ri", "jnt_lips_r", region::lips, corners);
        const auto lip = [&](bool lower, std::string_view from_left, std::string_view from_right,
            std::string_view to_left, std::string_view to_right, std::string_view centre,
            std::string_view centre_left, std::string_view centre_right)
        {
            add(from_left, to_left, region::lips, lips, 1, lower);
            add(from_right, to_right, region::lips, lips, 1, lower);
            // Symmetric centre support prevents moving only the outer lip
            // weights while the philtrum/centre of the mouth stays frozen.
            add(from_left, centre, region::lips, lips, 1, lower, from_right);
            add(from_left, centre_left, region::lips, lips, 1, lower);
            add(from_right, centre_right, region::lips, lips, 1, lower);
        };
        lip(false, "j_lip_top_le", "j_lip_top_ri", "jnt_lips_tl", "jnt_lips_tr", "jnt_lips_tc", "jnt_lips_tcl", "jnt_lips_tcr");
        lip(false, "j_lip_top_le", "j_lip_top_ri", "jnt_mouth_tl", "jnt_mouth_tr", "jnt_mouth_tc", "jnt_mouth_tcl", "jnt_mouth_tcr");
        lip(true, "j_lip_bot_le", "j_lip_bot_ri", "jnt_lips_bl", "jnt_lips_br", "jnt_lips_bc", "jnt_lips_bcl", "jnt_lips_bcr");
        lip(true, "j_lip_bot_le", "j_lip_bot_ri", "jnt_mouth_bl", "jnt_mouth_br", "jnt_mouth_bc", "jnt_mouth_bcl", "jnt_mouth_bcr");
        add("j_chin_skinroll", "jnt_chin_t", region::lips, lips, 1, true);
        std::sort(result.mapping.begin(), result.mapping.end(), [](const correspondence& a, const correspondence& b)
            { return a.destination < b.destination; });
        return result;
    }
}
