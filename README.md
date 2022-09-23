# avl-libjit

An implementation of just-in-time compilation of AVL trees as described 
[here](https://blog.christianperone.com/2009/11/a-method-for-jiting-algorithms-and-data-structures-with-llvm/) 
using [LibJIT](https://www.gnu.org/software/libjit/doc/libjit.html).

## Dependencies

* [LibJIT](https://www.gnu.org/software/libjit/doc/libjit.html)
* [GLib2.0](https://docs.gtk.org/glib/)

## Running

```shell
make
./avl-libjit
```