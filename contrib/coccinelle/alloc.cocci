@@
expression p;
@@
- p = xmalloc(sizeof(*p));
+ ALLOCATE(p);

@@
expression c;
expression p;
@@
- p = xcalloc(c, sizeof(*p));
+ CALLOCATE(p,c);

