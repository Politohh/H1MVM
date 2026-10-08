#include "component/demo/theater_models_policy.hpp"
#include "component/demo/theater_models_catalog.hpp"
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
    auto catalog=nlohmann::json::array();
    for(int i=1;i<=2615;++i) catalog.push_back({{"id",i},{"kind",i==2615?"body":"head"},
        {"model","head_test_"+std::to_string(i)},{"label","Test "+std::to_string(i)},
        {"faction",i<10?"SAS / Urban":"USMC"},{"role","assault"},{"collection","Default"}});
    const auto full=catalog_page(catalog,"head","","","All collections",1,"head_test_2","");
    assert(full["total"]==2614 && full["pages"]==654 && full["items"].size()==4 && full["headLabel"]=="Test 2");
    const auto last=catalog_page(catalog,"head","","","Default",1000000,"","");
    assert(last["page"]==654 && last["items"].size()==2 && last["items"][0]["id"]==2613);
    const auto faction=catalog_page(catalog,"head","SAS / Urban","assault","Default",-10,"","");
    assert(faction["total"]==9 && faction["page"]==1 && faction["collections"].size()==2);
    assert(catalog_page(catalog,"head","SAS / Urban","sniper","Default",1,"","")["items"].empty());
    assert(catalog_page(catalog,"body","USMC","assault","Default",1,"","")["items"].size()==1);
    std::cout<<"PASS head/body composition, equipment/tag preservation, missing head slot and restore\n";
    std::cout<<"PASS bounded native catalog pages, 2615 variants, filters, page limits and equipped labels\n";
}
