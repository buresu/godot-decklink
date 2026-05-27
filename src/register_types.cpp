#include "register_types.hpp"

#include <gdextension_interface.h>
#include <godot_cpp/classes/engine.hpp>
#include <godot_cpp/core/defs.hpp>
#include <godot_cpp/godot.hpp>

#include "decklink.hpp"
#include "decklink_device.hpp"
#include "decklink_input.hpp"
#include "decklink_output.hpp"

using namespace godot;

static DeckLink *decklink_singleton = nullptr;

void initialize_godot_decklink_module(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }

  GDREGISTER_CLASS(DeckLink);
  GDREGISTER_CLASS(DeckLinkDevice);
  GDREGISTER_CLASS(DeckLinkOutput);
  GDREGISTER_CLASS(DeckLinkInput);

  decklink_singleton = memnew(DeckLink);
  Engine::get_singleton()->register_singleton("DeckLink", decklink_singleton);
}

void uninitialize_godot_decklink_module(ModuleInitializationLevel p_level) {
  if (p_level != MODULE_INITIALIZATION_LEVEL_SCENE) {
    return;
  }

  if (decklink_singleton) {
    Engine::get_singleton()->unregister_singleton("DeckLink");
    memdelete(decklink_singleton);
    decklink_singleton = nullptr;
  }
}

extern "C" {
GDExtensionBool GDE_EXPORT
godot_decklink_init(GDExtensionInterfaceGetProcAddress p_get_proc_address,
                    GDExtensionClassLibraryPtr p_library,
                    GDExtensionInitialization *r_initialization) {
  godot::GDExtensionBinding::InitObject init_obj(p_get_proc_address, p_library,
                                                 r_initialization);
  init_obj.register_initializer(initialize_godot_decklink_module);
  init_obj.register_terminator(uninitialize_godot_decklink_module);
  init_obj.set_minimum_library_initialization_level(
      MODULE_INITIALIZATION_LEVEL_SCENE);
  return init_obj.init();
}
}
