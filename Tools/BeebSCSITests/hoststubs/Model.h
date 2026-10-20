#pragma once
enum class Model { B, FileStoreE01 };
extern Model MachineType;
inline bool IsFileStore(Model m) { return m == Model::FileStoreE01; }
