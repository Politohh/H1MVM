#include "component/demo/cod4_facial_retarget.hpp"
#include <fstream>
#include <iostream>
#include <limits>
using namespace cod4::face;
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::vector<std::uint8_t> bytes(const char* path)
{
    std::ifstream file(path,std::ios::binary);
    if(!file) throw std::runtime_error("Facial input could not be opened");
    return {(std::istreambuf_iterator<char>(file)),{}};
}
bool near(const pose& a,const pose& b)
{
    float direct{},opposite{};
    for(unsigned i=0;i<4;++i)
    {
        const float difference=a.rotation[i]-b.rotation[i],opposite_component=a.rotation[i]+b.rotation[i];
        direct+=difference*difference;opposite+=opposite_component*opposite_component;
    }
    if(std::min(direct,opposite)>1e-8f) return false;
    for(unsigned i=0;i<3;++i) if(std::abs(a.translation[i]-b.translation[i])>1e-4f) return false;
    return true;
}
int main(int argc,char** argv)
{
    try
    {
        const quat quarter_turn=normalized({0,0,1,1});
        skeleton source{{"j_head",-1,{}},{"j_jaw",0,{{0,0,0,1},{0,1,0}}},
            {"j_lip_bot_le",1,{{0,0,0,1},{1,0,0}}}};
        skeleton destination{{"j_head",-1,{{0,0,1,0},{100,200,300}}},
            {"jnt_mandible",0,{quarter_turn,{0,2,0}}},{"jnt_lips_bl",1,{quarter_turn,{2,0,0}}},
            {"jnt_teeth_lower",1,{{0,0,0,1},{0,0,1}}}};
        const std::array<correspondence,2> mapping{{{1,1},{2,2}}};
        clip neutral{10,10,false,{}};
        const auto neutral_output=retarget(source,neutral,0,0,destination,0,mapping,2);
        for(unsigned i=0;i<destination.size();++i) check(near(neutral_output[i],destination[i].rest),"Neutral expression changed destination rest pose");
        clip zero_offset{10,10,false,{{"j_jaw",{{0,{0,0,0,1}}},{{0,{0,0,0}}}},
            {"j_lip_bot_le",{{0,{0,0,0,1}}},{{0,{0,0,0}}}}}};
        const auto keyed_neutral=retarget(source,zero_offset,0,0,destination,0,mapping,2);
        for(unsigned i=0;i<destination.size();++i)
            check(near(keyed_neutral[i],destination[i].rest),"Zero XAnim translation collapsed a facial bind position");
        clip jaw{10,10,false,{{"j_jaw",{{0,{0,0,0,1}},{10,quarter_turn}},{{0,{0,0,0}},{10,{0,1,0}}}}}};
        const auto animated=retarget(source,jaw,1,0,destination,0,mapping,2);
        const auto global=model_space(destination,animated),rest=rest_model_space(destination);
        const auto expected_rotation=multiply(quarter_turn,destination[1].rest.rotation);
        check(near(animated[0],destination[0].rest),"Expression modified head/body transform");
        check(near(animated[3],destination[3].rest),"Unmapped jaw attachment lost local rest pose");
        check(near(animated[1],{expected_rotation,{0,4,0}}),"Rest orientation or anatomical translation scale ignored");
        const auto before_lip=relative(rest[0],rest[2]),after_lip=relative(global[0],global[2]);
        check(near(after_lip,{multiply(quarter_turn,before_lip.rotation),{before_lip.translation[0]-2,before_lip.translation[1]+4,before_lip.translation[2]}}),
            "Source inherited jaw movement applied twice or lost on lower lip");
        const auto repeat=retarget(source,jaw,1,0,destination,0,mapping,2);
        for(unsigned i=0;i<repeat.size();++i) check(near(repeat[i],animated[i]),"Retarget depends on previous evaluation/seek");
        const std::array<correspondence, 2> pivot_mapping{{{1,1}, {2,2,1,1}}};
        const auto pivot_output = retarget(source, jaw, 1, 0, destination, 0, pivot_mapping, 2);
        check(near(pivot_output[2], destination[2].rest), "Lower lip copied the source jaw lever arm instead of inheriting the destination jaw");
        check(near(pivot_output[1], animated[1]), "Jaw-relative lip altered the jaw expression");
        auto regional = pivot_mapping;
        regional[0].area = region::jaw; regional[1].area = region::lips;
        expression_strength half; half.values[static_cast<unsigned>(region::jaw)] = 0.5f;
        const auto reduced = retarget(source, jaw, 1, 0, destination, 0, regional, 2, half);
        check(near(reduced[1], {multiply(interpolate({0,0,0,1},quarter_turn,0.5f),destination[1].rest.rotation),{0,3,0}}),
            "Jaw rotation/translation strength was not attenuated consistently");
        check(near(reduced[2], destination[2].rest), "Lip gain attenuated inherited jaw motion twice");
        expression_strength off; off.values.fill(0);
        const auto disabled = retarget(source,jaw,1,0,destination,0,regional,2,off);
        for(unsigned i=0;i<disabled.size();++i) check(near(disabled[i],destination[i].rest),"Zero expression strengths changed the bind pose");
        skeleton symmetric_source{{"j_head",-1,{}},{"j_jaw",0,{}},
            {"j_lip_bot_le",1,{{0,0,0,1},{-1,2,0}}},{"j_lip_bot_ri",1,{{0,0,0,1},{1,2,0}}}};
        skeleton symmetric_target{{"j_head",-1,{}},{"jnt_mandible",0,{}},
            {"jnt_lips_bc",1,{{0,0,0,1},{0,4,0}}}};
        const std::array<correspondence,1> centre_mapping{{{2,2,1,1,3}}};
        clip symmetric{10,10,false,{{"j_lip_bot_le",{},{{0,{-0.3f,-0.4f,0}}}},
            {"j_lip_bot_ri",{},{{0,{0.3f,-0.4f,0}}}}}};
        const auto centre = retarget(symmetric_source,symmetric,0,0,symmetric_target,0,centre_mapping,1);
        check(near(centre[2],{{0,0,0,1},{0,3.6f,0}}),"Centre lip moved sideways under a symmetric expression");
        const auto centre_repeat=retarget(symmetric_source,symmetric,0,0,symmetric_target,0,centre_mapping,1);
        check(near(centre[2],centre_repeat[2]),"Regional transfer accumulated movement across paused frames");
        bool rejected{};
        const std::array<correspondence,2> duplicate{{{1,1},{2,1}}};
        try{retarget(source,jaw,0,0,destination,0,duplicate,1);}catch(const std::runtime_error&){rejected=true;}
        check(rejected,"Duplicate destination control accepted");
        rejected=false;source[1].parent=1;
        try{retarget(source,jaw,0,0,destination,0,mapping,1);}catch(const std::runtime_error&){rejected=true;}
        check(rejected,"Cyclic/forward parent accepted");
        for(int i=1;i<argc;++i)
        {
            const auto input=bytes(argv[i]);const auto bones=decode_rest(input);const auto bind=rest_model_space(bones);
            unsigned faces{};
            for(const auto& b:bones) faces+=facial_bone(b.name);
            const bool masked=std::string_view(argv[i]).find("mich_stripes_nomex")!=std::string_view::npos;
            check(faces==(masked?14u:20u),"Stock head rest control coverage changed");
            const auto neck=std::find_if(bones.begin(),bones.end(),[](const bone& b){return b.name=="j_neck";});
            const auto head=std::find_if(bones.begin(),bones.end(),[](const bone& b){return b.name=="j_head";});
            check(neck!=bones.end()&&head!=bones.end()&&head->parent==neck-bones.begin(),"Source head hierarchy changed");
            for(const auto& p:bind)for(float v:p.translation)check(std::isfinite(v),"Non-finite source bind position");
            auto truncated=input;truncated.pop_back();rejected=false;
            try{decode_rest(truncated);}catch(const std::runtime_error&){rejected=true;}
            check(rejected,"Truncated rest asset accepted");
            std::cout<<"PASS "<<argv[i]<<" bones="<<bones.size()<<" face_controls="<<faces<<'\n';
        }
        std::cout<<"PASS neutral preservation, regional strengths, destination jaw pivot, symmetric centre lips, pause/seek determinism and invalid mappings\n";
        return 0;
    }
    catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
}
