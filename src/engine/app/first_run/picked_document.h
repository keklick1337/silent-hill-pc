#pragma once

#include <string>

namespace sh::first_run {

// The file the system's file dialog gave, as a path the importer can open. On Android the dialog answers with a
// content:// URI of the Storage Access Framework; the activity (android/, SilentHillActivity.openDocument) opens the
// document and hands back a path to it, or to a copy of it in the app's storage when the system lets no path reach
// the open document. Anything else is returned as it is.
std::string picked_document(const std::string& picked);

// True for a path picked_document made: it lasts only as long as this run, so it is not remembered in config.cfg.
bool is_picked_document(const std::string& path);

// Android: calls the activity's static String name(String) through JNI; "" when it fails or elsewhere.
std::string activity_call(const char* name, const std::string& argument);

// Removes the copies picked_document made, once the import no longer needs them.
void forget_picked_documents();

}  // namespace sh::first_run
