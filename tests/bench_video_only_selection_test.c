#include <assert.h>
#include <string.h>

#include "array_list.h"
#include "ss4s_modules.h"

SS4S_ModuleCheckFlag __wrap_SS4S_ModuleCheck(const char *id, SS4S_ModuleCheckFlag flags) {
    assert(id != NULL);
    assert(strcmp(id, "ndl-webos4") == 0);
    return (SS4S_ModuleCheckFlag)(flags & SS4S_MODULE_CHECK_VIDEO);
}

int main(void) {
    array_list_t modules;
    array_list_init(&modules, sizeof(SS4S_ModuleInfo), 1);
    assert(modules.data != NULL);

    SS4S_ModuleInfo *module = array_list_add(&modules, -1);
    assert(module != NULL);
    memset(module, 0, sizeof(*module));
    module->id = "ndl-webos4";
    module->group = "ndl";
    module->name = "webOS 4 NDL";
    module->has_audio = true;
    module->has_video = true;

    SS4S_ModulePreferences preferences = {
        .audio_module = MODULE_PREFERENCE_AUTO,
        .video_module = "ndl-webos4",
    };
    SS4S_ModuleSelection selection = {0};

    bool complete = SS4S_ModulesSelect(&modules, &preferences, &selection, true);

    assert(!complete);
    assert(selection.video_module == module);
    assert(selection.audio_module == NULL);

    array_list_deinit(&modules);
    return 0;
}
