#include "component/demo/theater_dof.hpp"
#include <cassert>
#include <iostream>

int main()
{
    using namespace theater_editor::dof;
    // Graphics quality alone does not prove the renderer has recreated its targets.
    for (int quality = 0; quality <= 3; ++quality)
        for (unsigned flags = 0; flags < 2048; ++flags)
        {
            const bool ready = resources_ready(quality, flags);
            assert(ready == (quality != 0 && !(flags & 32)));
            assert(allows_edit("r_dof_tweak", 1, ready) == ready);
            assert(allows_edit("r_dof_tweak", 0, ready));
            assert(allows_edit("r_dof_tweak", 2, ready));
            // Parameters may be staged while unavailable, without enabling the override.
            assert(allows_edit("r_dof_physical_fstop", 1, ready));
        }
    assert(activates_override("r_dof_physical_fstop"));
    assert(activates_override("r_dof_physical_focusDistance"));
    assert(activates_override("r_dof_physical_bokehEnable"));
    assert(!activates_override("r_dof_tweak"));
    assert(!activates_override("r_dof_enable"));
    assert(!activates_override("r_dof_physical_enable"));
    assert(!activates_override("r_filmTweakBrightness"));
    std::cout << "DOF resource/override safety tests passed\n";
}
