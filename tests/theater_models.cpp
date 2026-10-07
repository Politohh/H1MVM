#include "component/demo/theater_models_policy.hpp"
#include <cassert>
#include <iostream>
int main()
{
    using namespace theater_models;
    composition c; c.body="body_arab_desert_assault_mp_camo";
    c.models[0]="weapon_m4";c.tags[0]="tag_weapon";
    c.models[1]="head_arab_desert_assault_mp";c.tags[1]="j_head";
    c.models[2]="equipment_radio";c.tags[2]="tag_back";
    auto sas=replace(c,"head_sas_ct_assault_mp","body_sas_ct_assault_mp_camo");
    assert(sas && sas->models[1]=="head_sas_ct_assault_mp" && sas->body=="body_sas_ct_assault_mp_camo");
    assert(sas->models[0]==c.models[0] && sas->models[2]==c.models[2] && sas->tags==c.tags);
    assert(replace(c,"","")==c); // Revert original without retaining any swapped pieces.
    auto body=replace(c,"","body_sas_ct_assault_mp_camo");assert(body && body->models==c.models);
    c.models[1]="equipment_mask";c.tags[1]="tag_mask";
    auto extra=replace(c,"head_sas_ct_smg_mp","");
    assert(extra && extra->models[3]=="head_sas_ct_smg_mp" && extra->tags[3].empty() && extra->models[1]=="equipment_mask");
    c.models[1]="head_arab_desert_assault_mp"; c.tags[1].clear();
    assert(replace(c,"head_sas_ct_assault_mp","")->tags[1].empty());
    for(auto& part:c.models) part="equipment_full";
    assert(!replace(c,"head_sas_ct_smg_mp","") && replace(c,"","body_sas_ct_assault_mp_camo"));
    assert(c.body=="body_arab_desert_assault_mp_camo"); // No mutation of recorded source.
    std::cout<<"PASS head/body composition, equipment/tag preservation, missing head slot and restore\n";
}
