---
layout: post
title: An Introduction to Reflection in C++
---

Stop me if you've heard this one before. You are working on a messaging middleware, a game engine, a UI library, or any other large software project that has to deal with an ever-growing, ever-changing number of objects. These objects have many different qualities but can be grouped by their functionality: they can be sent across the network or collided with or rendered.

Because you are a good programmer who believes in the DRY principle, you want to write the "action" code that *does the stuff* on these objects without repetition, and plug in specific Message types or Renderable types into your generic pipeline at the appropriate places. It would be really nice to compose objects hierarchally: for example, if I had a widget class composed of several different renderable Rectangles, I want to be able to automatically generate the rendering code for my widget based on the existing rendering logic for its constituent shapes.

In C++, this is harder than it sounds. You start by using inheritance and a parent type that provides a unified interface for the "action" code. But you quickly run into issues with interface composibility and slicing, and if latency or code size are critical to your application, virtual functions will give you a performance hit.

You hear that using an external code generation tool (like Protobuf for message encoding, or the QT Meta-Object compiler for UI widgets) is a common practice for these kinds of projects. This approach may work for certain use cases but feels *wrong* for a few reasons: brittle, inextensible, intrusive, and redundant.

Because you're in touch with the latest and greatest features of C++, you hear about this thing called Concepts that will help the shortcomings of inheritance for providing a generic interface to related types. But even if you're working with a compiler that implements concepts, introducing a "Serialiazable" or "Renderable" concept doesn't solve some of your fundamental problems. When you want to compose objects hierarchically, you still don't have a good way of automatically detecting when any of the members of your type also conform to that Concept.

This is not a new problem. See this excerpt from [The Art of Unix Programming by Eric S. Raymond](http://citeseerx.ist.psu.edu/viewdoc/download?doi=10.1.1.629.5901&rep=rep1&type=pdf), circa 2003, that describes this issue as it manifested itself in the `fetchmailconf` program:

> The author considered writing a glue layer that would explicitly know about the structure of
> all three classes and use that knowledge to grovel through the initializer creating matching
> objects, but rejected that idea because new class members were likely to be added over time
> as the configuration language grew new features. If the object-creation code were written in
> the obvious way, it would be fragile and tend to fall out of synchronization when the class
> definitions changed.  Again, a recipe for endless bugs.
> 
> The better way would be data-driven programming — code that would analyze the shape and
> members of the initializer, query the class definitions themselves about their members, and
> then impedance-match the two sets.
> 
> Lisp programmers call this introspection; in object-oriented languages it’s called metaclass
> hacking and is generally considered **fearsomely esoteric, deep black magic.**

There's another name for this arcane art which we'll use in this article: reflection. You probably think there's already far too much black magic in C++. But I want to convince you that we need reflection in the language today, especially compile-time introspection, and that it's not a fearsome, esoteric mystery, but a useful tool that can make your codebase cleaner and more effective.

## The story so far
Before we dive too far in, let's define some terms. As we know from my previous blog post, names are frustratingly hard to pin down, so let's not quibble too much over definitions so that we can get to the real content.

**Introspection** is the ability to inspect a type and retrieve its various qualities. You might want to introspect an object's data members, member functions, inheritance hierarchy, etc. And you might want to introspect different things at compile time and runtime.

**Metaobjects** are the result of introspection on a type: a handle containing the metadata you requested from introspection. If the reflection implementation is good, this metaobject handle should be lightweight or zero-cost at runtime. TODO: Example

**Reification** is a fancy word for "making something a first-class citizen", or "making something concrete". We will use it to mean mapping from the reflected representation of objects (metaobjects) to concrete types or identifiers. TODO: Example

Actually, there are a few ways you can achieve reflection in the language today, whether you're using C++98 or C++14. These methods have their advantages and disadvantages, but the fundamental issue overall is that we have no standardized language-level reflection facilities that solve the use case described in the introduction.

### RTTI
Run-time type information/identification is a controversial C++ feature, commonly reviled by performance maniacs and zero-overhead zealots. If you've ever used `dynamic_cast`, `typeid`, or `type_info`, you were using RTTI. If you've ever compiled code with `-fno-rtti`, you were probably working in a codebase that explictly avoided these features because of the cost it incurs.

RTTI is C++'s built-in mechanism for matching relationships between types at runtime. That relationship can be equality or a relationship via inheritance. Therefore it implements a kind of limited runtime introspection.

TODO:
As a side note, LLVM implements an alternative to RTTI where the lookup in `dynamic_cast` is limited to.
I'm still not sure why C++ doesn't have language-level support for this.

As a reflection mechanism, RTTI doesn't have many advantages besides the fact that it's standardized and available in any of the three major compilers (Clang, gcc, MSVC). It is not as powerful as the other reflection mechanisms we are about describe and doesn't fit the problem statement we made above. Still, the general problem of mapping from runtime information to compile-time types is important for many interesting applications.

### Macros
We love and hate C++ because the language allows us to do **almost anything**. Macros are the most flexible tool in the arsenal of C++'s foot-guns.

There's a lot you can do with macros that you could instead do with templates, which grants you type safety, better error handling, and often better optimizations than macros. However, reflection can't be achieved with templates alone and must leverage macros (until C++17, as we'll see in the next section).

The Boost C++ libraries offer a few utility libraries for template metaprogramming, including MPL, Fusion, and Hana. Fusion and Hana both offer macros that can adapt a user-defined POD-type into an introspectible structure which tuple-like access.

TODO: Example

TODO: Overview of implementation

Why did the authors of these libraries write these macros in the first place? Bragging rights, of course!

Actually, tuple-like access for structs is incredibly useful for generic programming. It allows you to perform operations on every member of a type, or a subset of members conforming to a particular concept, without having to know the specific names of each member. Again, this is a feature that you can't get with inheritance, templates, and concepts alone.

The major disadvantage of these macros is that you need to inject a call to `ADAPT_STRUCT` somewhere in your code in the global scope. (TODO)

### `magic_get`

The current pinnacle of POD introspection in C++14 is a library called `magic_get` by Antony Polukhin. Unlike Fusion or Hana's approach, `magic_get` doesn't require calling a `DEFINE_STRUCT` or `ADAPT_STRUCT` macro for each struct we want to introspect. Instead, you can put an object of any POD type into the `magic_get` utility functions and access it like a tuple.

TODO: example

Another exciting thing about `magic_get` is that it provides a C++17 implementation that uses no macros, only templates and the new structured bindings feature.

Let's [take a peek under the hood]( https://github.com/apolukhin/magic_get/blob/develop/include/boost/pfr/detail/core17_generated.hpp) to see how Antony achieves this:

```c++
template <class T>
constexpr auto as_tuple_impl(T&& /*val*/, size_t_<0>) noexcept {
  return sequence_tuple::tuple<>{};
}


template <class T>
constexpr auto as_tuple_impl(T&& val, size_t_<1>) noexcept {
  auto& [a] = std::forward<T>(val);
  return ::boost::pfr::detail::make_tuple_of_references(a);
}

template <class T>
constexpr auto as_tuple_impl(T&& val, size_t_<2>) noexcept {
  auto& [a,b] = std::forward<T>(val);
  return ::boost::pfr::detail::make_tuple_of_references(a,b);
}
```

These specializations of `as_tuple_impl` repeat this same pattern for a total of 101 specializations (some liberty was taken with line breaks):

```c++
template <class T>
constexpr auto as_tuple_impl(T&& val, size_t_<99>) noexcept {
  auto& [
    a,b,c,d,e,f,g,h,j,k,l,m,n,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,J,K,L,M,N,
    P,Q,R,S,T,U,V,W,X,Y,Z,
    aa,ab,ac,ad,ae,af,ag,ah,aj,ak,al,am,an,ap,aq,ar,as,at,au,av,aw,ax,ay,az,aA,
    aB,aC,aD,aE,aF,aG,aH,aJ,aK,aL,aM,aN,aP,aQ,aR,aS,aT,aU,aV,aW,aX,aY,aZ,
    ba,bb,bc
  ] = std::forward<T>(val);

  return ::boost::pfr::detail::make_tuple_of_references(
    a,b,c,d,e,f,g,h,j,k,l,m,n,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,J,K,L,M,N,
    P,Q,R,S,T,U,V,W,X,Y,Z,
    aa,ab,ac,ad,ae,af,ag,ah,aj,ak,al,am,an,ap,aq,ar,as,at,au,av,aw,ax,ay,az,
    aA,aB,aC,aD,aE,aF,aG,aH,aJ,aK,aL,aM,aN,aP,aQ,aR,aS,aT,aU,aV,aW,aX,aY,aZ,
    ba,bb,bc
  );
}

template <class T>
constexpr auto as_tuple_impl(T&& val, size_t_<100>) noexcept {
  auto& [
    a,b,c,d,e,f,g,h,j,k,l,m,n,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,J,K,L,M,N,
    P,Q,R,S,T,U,V,W,X,Y,Z,
    aa,ab,ac,ad,ae,af,ag,ah,aj,ak,al,am,an,ap,aq,ar,as,at,au,av,aw,ax,ay,az,
    aA,aB,aC,aD,aE,aF,aG,aH,aJ,aK,aL,aM,aN,aP,aQ,aR,aS,aT,aU,aV,aW,aX,aY,aZ,
    ba,bb,bc,bd
  ] = std::forward<T>(val);

  return ::boost::pfr::detail::make_tuple_of_references(
    a,b,c,d,e,f,g,h,j,k,l,m,n,p,q,r,s,t,u,v,w,x,y,z,A,B,C,D,E,F,G,H,J,K,L,M,N,
    P,Q,R,S,T,U,V,W,X,Y,Z,
    aa,ab,ac,ad,ae,af,ag,ah,aj,ak,al,am,an,ap,aq,ar,as,at,au,av,aw,ax,ay,az,aA,
    aB,aC,aD,aE,aF,aG,aH,aJ,aK,aL,aM,aN,aP,aQ,aR,aS,aT,aU,aV,aW,aX,aY,aZ,
    ba,bb,bc,bd
  );
}
```

What's going on here? Antony is decomposing a flat struct of type T into a bunch of references named after consecutive letters in the alphabet. He then forwards those fields on to a tuple of references of his custom tuple type. This is the core mechanism for "converting" from a struct to a tuple. But since there's no way to destructure an arbitrary number of fields ("variadic structured bindings"?), this code can't be written in a generic fashion, and the library instead resorts to generating these specializations via a [Python script](https://github.com/apolukhin/magic_get/blob/develop/misc/generate_cpp17.py). Since there are 101 specializations of this struct decomposition utility, the library cannot introspect a struct which has a level with more than 101 fields (TODO: fact-check).

This copy and paste/code generation technique is also seen in the macro-based solutions for introspection. If we had language-level reflection in C++, these techniques would not be necessary, and the limit on the number of fields would be greatly increased (although more than 100 fields in a struct seems very rare to me). The compile time cost for reflection would probably also be greatly reduced.

### Function reflection: `HippoMocks` and `tromploeil`
TODO

### Compiler tooling
The Clang C++ compiler isn't just a standalone executable that takes your source code and spits out an executable. You can use `libclang` or `LibTooling` to write your own tools using an interface to the parsed AST of C++ code. Code that understands its own AST is not only using reflection, it is accessing the most powerful form of introspection, since the introspection API can use all of the information used by the compiler.

[`siplasplas`](https://github.com/Manu343726/siplasplas) is a reflection engine written by Manu Sanchez that uses libclang to generate meta-object headers for C++ types, enabling static and dynamic reflection.

The disadvantage of this approach is that you are locked in by the API of a particular compiler. Even if you wanted to make a compatibility layer for another compiler's tooling... you probably can't, since GCC and MSVC don't offer an analogous modular library for AST access like Clang does.

## Reflection in other languages
Although C++ programmers ~~usually~~ sometimes look down upon other programming communities, I think it's important for us to understand how things are done in other languages before we pass judgement on language features. This applies to everyone, from language designers sitting on the committee who are trying to make the language better, to everyday programmers who could choose to specialize in a different language and move on from the community if their comparison leads them elsewhere.

The earlier quote from Eric S. Raymond mentions introspection in Lisp. It continues on to compare other languages that are common in the Unix world:

> Most object-oriented languages don’t support it [reflection] at all; in those that do (Perl being
> one), it tends to be a complicated and fragile undertaking. Python’s facilities for metaclass
> hacking are unusually accessible."

However, The Art of Unix Programming is showing its age, as several other mainstream general-purpose programming languages offer reflection facilities with various degrees of power.

### Python
As ESR says, the Python reflection interface is user-friendly and flexible, but this comes at the cost of performance. There are a number of utility functions for querying an object's metadata such as `type`, `hasattr`, and the very powerful `dir` command, which lists all names in the current scope if given no arguments, or if given the name of an object returns a list of the object's attributes. You can even use `dir` on modules to see what functions and objects a certain module imports. You can use `setattr` to add new attributes to an object, or `delattr` to remove attributes.

One of my favorite Python libraries is [Beautiful Soup](https://www.crummy.com/software/BeautifulSoup/bs4/doc/). It is an HTML/XML parser that makes heavy use of Python reflection facilities. When you parse a document with Beautiful Soup, the XML tags and attributes of the resulting objects are translated directly to Python attributes. For example, an XML tree that looks like this:

```
<html>
 <head>
  <title>
   The Dormouse's story
  </title>
 </head>
```

... can be accessed like this if the document is parsed into an object called `soup`:

```
soup.title.string
# u'The Dormouse's story'

soup.title.parent.name
# u'head'
```

This access will work for arbitrary XML tags (as long as the XML is well-formed).

Implementing such a library in C++ today is basically impossible, because it involves translating runtime values into member identifiers. If you're not yet convinced that reflection would be great for parser libraries, consider that the most powerful parser library in C++ today is Boost Spirit, which makes heavy use of the Boost Fusion reflection macro.

TODO: More on performance cost?

### Java
The Java reflection API is highly structured (unlike Python) and quite extensive. It offers a set of definitions for classes representing language constructs and functions for extracting information about those language constructs.

Here's an example that creates Class objects from a list of names, adapted from [this tutorial](https://docs.oracle.com/javase/tutorial/reflect/class/classMembers.html):

```java
public static void main(String... args) {
    Class<?> c = Class.forName(args[0]);
    out.format("Class:%n  %s%n%n", c.getCanonicalName());

    Package p = c.getPackage();
    out.format("Package:%n  %s%n%n",
         (p != null ? p.getName() : "-- No Package --"));

    for (int i = 1; i < args.length; i++) {
        switch (ClassMember.valueOf(args[i])) {
        case CONSTRUCTOR:
            printMembers(c.getConstructors(), "Constructor");
            break;
        case FIELD:
            printMembers(c.getFields(), "Fields");
            break;
        case METHOD:
            printMembers(c.getMethods(), "Methods");
            break;
        case CLASS:
            printClasses(c);
            break;
        case ALL:
            printMembers(c.getConstructors(), "Constuctors");
            printMembers(c.getFields(), "Fields");
            printMembers(c.getMethods(), "Methods");
            printClasses(c);
            break;
        default:
            assert false;
        }
    }
}
```

Java reflection incurs a runtime cost because, as you can see, the objects resulting from reflection are plain runtime constructs. This is unacceptable to many of us performance freaks in the C++ community: for cases where the reflection data is not needed at runtime, this is not a zero-cost abstraction.

TODO: Data on performance cost

### Honorable mentions
The C# reflection system is somewhat similar to Java's: it allows type lookup by string identifiers and reifies types as runtime objects. It has utilities for getting and setting reflected fields, and even invoking reflected functions. It was designed with the use case of "runtime assemblies" in mind; that is, the ability to load dynamic shared libraries at runtime and use the reflection interface to reach into the API of the shared library. For more information I recommend [these](http://www.codeguru.com/csharp/csharp/cs_misc/reflection/article.php/c4257/An-Introduction-to-Reflection-in-C.htm) [articles](https://msdn.microsoft.com/en-gb/library/f7ykdhsy.aspx).

Go was designed to be a C++-killer. It has a few advantages over C++, like a bigger standard library, but its facilities for generics and metaprogramming are far cry from modern C++. This (now somewhat dated) article on [why Go doesn't need generics](https://appliedgo.net/generics/) might make you mad if you love generic programming (like me), but it also gives an introduction to interfaces and reflection, the stand-ins for language-level generics. The Go [`reflect`](https://golang.org/pkg/reflect/) package implements runtime reflection. It is essentially a utility for retrieving a runtime identifier for the concrete type stored in an interface. If you're curious about this, read about the Go [laws of reflection](https://blog.golang.org/laws-of-reflection).

Rust is a notable competitor to C++ in many areas. Like C++ it currently lacks language-level reflection support, but it is possible to hack an implementation using some combination of AST plugins, attributes, and macros.

## The road to standardization: `reflexpr` and `operator$`

The C++ ISO Standards Committee has a study group on reflection and metaprogramming, SG7, which is evaluating multiple reflection proposals targeting C++20.

The `reflexpr` proposal, by Matúš Chochlík, Axel Naumann, and David Sankel, introduces several "metaobjects" which are accessed by passing a type to the new `reflexpr` operator. I recommend ["Static Reflection in a Nutshell"](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0578r0.html) for a brief overview of the design and [Static reflection](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0194r3.html) for a longer overview. You can find instructions for building Matúš's fork of Clang which implements `reflexpr`, read his documentation, and explore his `mirror` utility library [here](http://matus-chochlik.github.io/mirror/doc/html/index.html).

Andrew Sutton and Herb Sutter wrote, ["A design for static reflection"](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0590r0.pdf), which introduces the reflection operator, `$`, as a way of getting object metadata out of a class, namespace, etc. (Using `$` has been argued out of favor because it is common in legacy code, particularly in code generation and template systems which are not necessarily valid C++ but produce C++ sources.) You can explore Andrew Sutton's Clang fork implementing the proposal [here](https://github.com/asutton/clang-reflect).

You don't have to read the papers to continue enjoying this blog post, because I'll just tell you the interesting bits. The fundamental design difference between these two papers is whether the result of the reflection operator should be a value or a type.

TODO:

Regardless of whether you prefer type-based metaprogramming or value-based metaprogramming...

## Conclusion (for now)

I believe reflection is an indispensible tool for generic libraries and huge frameworks that deal with lots of different types and complex relationships between those types. Standardizing reflection in C++ will help make this feature more accessible to the average programmer, make C++ programmers more productive, and help the language catch up to other general-purpose programming languages and distinguish itself from competing languages by providing a zero-overhead reflection facility.

Tune in next time for part 2 of this series, where I'll cover C++ code that demonstrate what you can do with reflection!
