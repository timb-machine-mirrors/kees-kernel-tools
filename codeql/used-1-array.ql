import cpp

/* Will this find flex arrays in unions? */
class TrailingSizeOneArrayMember extends Field {
  TrailingSizeOneArrayMember() {
    exists(Struct s |
      this = s.getCanonicalMember(max(int j | s.getCanonicalMember(j) instanceof Field | j))
    ) and
    this.getUnspecifiedType() instanceof ArrayType and
    this.getUnspecifiedType().(ArrayType).getArraySize() = 1
  }
}

from
  Struct struct,
  TrailingSizeOneArrayMember member,
  Expr access
where
  member = struct.getAField() and
  member.getAnAccess() = access and
  (
    /* array[NNN] */
    exists(ArrayExpr array, Expr index |
      array.getArrayBase() = access and
      array.getArrayOffset() = index and
      /*
       * If it's a dynamic array index, we have to assume we're treating
       * the array as a flexible array (or at the very least, we can't
       * make any assumptions about it).
       * If it's a literal, we can ignore all instances of a "0" index,
       * which would not be out of bounds for a 1-element array.
       */
       (not index.isConstant() or index.getValue().toInt() != 0)
   )
   or
   /* array + NNN */
   any(PointerAddExpr add).getLeftOperand() = access
  )
select
  "struct " + struct.getName() + "::" + member.getName() as name,
  access.getLocation() as location
