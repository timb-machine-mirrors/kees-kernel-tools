// Options: --no-includes --include-headers

@direct@
type TYPE;
TYPE *P;
expression GFP;
expression COUNT;
expression FLEX;
identifier ALLOC =~ "kv?[mz]alloc";
fresh identifier ALLOC_OBJ = ALLOC ## "_obj";
@@

(
-	P = ALLOC(\(sizeof(*P)\|sizeof(TYPE)\), GFP)
+	ALLOC_OBJ(P, GFP)
|
-	P = ALLOC(struct_size(P, FLEX, COUNT), GFP)
+	ALLOC_OBJ(P, FLEX, COUNT, GFP)
|
-	P = kmalloc_array(COUNT, \(sizeof(*P)\|sizeof(TYPE)\), GFP)
+	kmalloc_obj(P, COUNT, GFP)
|
-	P = kmalloc_array(\(sizeof(*P)\|sizeof(TYPE)\), COUNT, GFP)
+	kmalloc_obj(P, COUNT, GFP)
|
-	P = kcalloc(COUNT, \(sizeof(*P)\|sizeof(TYPE)\), GFP)
+	kzalloc_obj(P, COUNT, GFP)
|
-	P = kcalloc(\(sizeof(*P)\|sizeof(TYPE)\), COUNT, GFP)
+	kzalloc_obj(P, COUNT, GFP)
|
-	P = kvmalloc_array(COUNT, \(sizeof(*P)\|sizeof(TYPE)\), GFP)
+	kvmalloc_obj(P, COUNT, GFP)
|
-	P = kvmalloc_array(\(sizeof(*P)\|sizeof(TYPE)\), COUNT, GFP)
+	kvmalloc_obj(P, COUNT, GFP)
|
-	P = kvcalloc(COUNT, \(sizeof(*P)\|sizeof(TYPE)\), GFP)
+	kvzalloc_obj(P, COUNT, GFP)
|
-	P = kvcalloc(\(sizeof(*P)\|sizeof(TYPE)\), COUNT, GFP)
+	kvzalloc_obj(P, COUNT, GFP)
)

@assign_struct_size@
type TYPE;
TYPE *P;
expression GFP;
expression COUNT;
expression FLEX;
expression SIZE;
identifier ALLOC =~ "kv?[mz]alloc";
fresh identifier ALLOC_OBJ = ALLOC ## "_obj";
@@

(
	SIZE =
-		struct_size(P, FLEX, COUNT);
+		ALLOC_OBJ(P, FLEX, COUNT, GFP);
-	P = ALLOC(SIZE, GFP);
|
-	SIZE = struct_size(P, FLEX, COUNT);
	... when != SIZE
-	P = ALLOC(SIZE, GFP);
+	SIZE = ALLOC_OBJ(P, FLEX, COUNT, GFP);
)

@assign_sizeof@
type TYPE;
TYPE *P;
expression GFP;
expression SIZE;
identifier ALLOC =~ "kv?[mz]alloc";
fresh identifier ALLOC_OBJ = ALLOC ## "_obj";
@@

(
	SIZE =
-		sizeof(*P);
-	P = ALLOC(SIZE, GFP);
+		ALLOC_OBJ(P, GFP);
|
	SIZE =
-		sizeof(TYPE);
-	P = ALLOC(SIZE, GFP);
+		ALLOC_OBJ(P, GFP);
|
-	SIZE = sizeof(*P);
	... when != SIZE
-	P = ALLOC(SIZE, GFP);
+	SIZE = ALLOC_OBJ(P, GFP);
|
-	SIZE = sizeof(TYPE);
	... when != SIZE
-	P = ALLOC(SIZE, GFP);
+	SIZE = ALLOC_OBJ(P, GFP);
)
