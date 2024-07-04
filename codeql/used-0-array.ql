import cpp

/* Will this find flex arrays in unions? */
class TrailingSizeZeroArrayMember extends Field {
  TrailingSizeZeroArrayMember() {
    exists(Struct s |
      this = s.getCanonicalMember(max(int j | s.getCanonicalMember(j) instanceof Field | j))
    ) and
    this.getUnspecifiedType() instanceof ArrayType and
    /* This should correctly find 0-sized arrays. A C99 flexible array is visible via:
     * "not this.getUnspecifiedType().(ArrayType).hasArraySize()"
     */
    this.getUnspecifiedType().(ArrayType).getArraySize() = 0
  }
}

from
  Struct struct,
  TrailingSizeZeroArrayMember member,
  Expr access
where
  member = struct.getAField() and
  member.getAnAccess() = access and
  (
    /* array[NNN] */
    exists(ArrayExpr array |
      array.getArrayBase() = access
    )
    or
    /* array + NNN */
    any(PointerAddExpr add).getLeftOperand() = access
  )
select
  "struct " + struct.getName() + "::" + member.getName() as name,
  access.getLocation() as location
