export module cpplox:ObjectFwd;

namespace cpplox {

export class Obj;
export class ObjString;
export class ObjUpvalue;
export class ObjFunction;
export class ObjClosure;
export class ObjNative;
export class ObjClass;
export class ObjInstance;
export class ObjBoundMethod;

export auto release_object(Obj * obj) -> void;

} // namespace cpplox
