---
layout: post
title: An Introduction to Reflection in C++
hidden: true
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

**Metaobjects** are the result of introspection on a type: a handle containing the metadata you requested from introspection. If the reflection implementation is good, this metaobject handle should be lightweight or zero-cost at runtime.

**Reification** is a fancy word for "making something a first-class citizen", or "making something concrete". We will use it to mean mapping from the reflected representation of objects (metaobjects) to concrete types or identifiers.

Actually, there are a few ways you can achieve reflection in the language today, whether you're using C++98 or C++14. These methods have their advantages and disadvantages, but the fundamental issue overall is that we have no standardized language-level reflection facilities that solve the use case described in the introduction.

### RTTI
Run-time type information/identification is a controversial C++ feature, commonly reviled by performance maniacs and zero-overhead zealots. If you've ever used `dynamic_cast`, `typeid`, or `type_info`, you were using RTTI.

RTTI is C++'s built-in mechanism for matching relationships between types at runtime. That relationship can be equality or a relationship via inheritance. Therefore it implements a kind of limited runtime introspection. However, C++ RTTI requires using vtables (virtual function tables), the array of pointers used to dispatch virtual functions to their runtime-determined implementations. Compiling with `-fnortti` is a common optimization to disable generating vtables, which saves the compiler a lot of work in code generation. But it also disables the "idiomatic" C++ way of achieving runtime polymorphism.

A bit of a sidebar: why is RTTI idiomatic C++ when there are better alternatives? LLVM implements an alternative to RTTI which works on classes without vtables, but requires a little extra boilerplate (see the [LLVM Programmer's Manual](http://llvm.org/docs/ProgrammersManual.html#the-isa-cast-and-dyn-cast-templates)). And, if you squint at the extra boilerplate used in this technique, you can imagine formalizing this with a generic library and even adding more ideas like dynamic concepts (sometimes called interfaces in other languages). In fact, Louis Dionne is working on a [experimental library](https://github.com/ldionne/dyno) that does just that.

As a reflection mechanism, RTTI doesn't have many advantages besides the fact that it's standardized and available in any of the three major compilers (Clang, gcc, MSVC). It is not as powerful as the other reflection mechanisms we are about describe and doesn't fit the problem statement we made above. Still, the general problem of mapping from runtime information to compile-time types is important for many interesting applications.

### Macros
We love and hate C++ because the language allows us to do **almost anything**. Macros are the most flexible tool in the arsenal of C++'s foot-guns.

There's a lot you can do with macros that you could instead do with templates, which grants you type safety, better error handling, and often better optimizations than macros. However, reflection can't be achieved with templates alone and must leverage macros (until C++17, as we'll see in the next section).

The Boost C++ libraries offer a few utility libraries for template metaprogramming, including MPL, Fusion, and Hana. Fusion and Hana both offer macros that can adapt a user-defined POD-type into an introspectible structure which tuple-like access. I'll focus on Hana because I'm more familiar with the library and it's a newer library. However, the macro offered by Fusion has the same interface.

[The Hana documentation](http://boostorg.github.io/hana/index.html#tutorial-introspection-adapting) gives a full overview of these macros. You can either define an introspectible struct with `BOOST_HANA_DEFINE_STRUCT`:

```c++
struct Person {
  BOOST_HANA_DEFINE_STRUCT(Person,
    (std::string, name),
    (std::string, last_name),
    (int, age)
  );
};
```

Or, if you'd like to introspect a type defined by another library, you can use `BOOST_HANA_ADAPT_STRUCT`:

```c++
BOOST_HANA_ADAPT_STRUCT(Person, name, last_name, age);
```

After doing this, you can access the struct in a generic fashion. For example, you can iterate over the fields of the struct using `hana::for_each`:

```c++
Person jackie{"Jackie", "Kay", 24};
hana::for_each(jackie, [](auto pair) {
  std::cout << hana::to<char const*>(hana::first(pair)) << ": "
            << hana::second(pair) << std::endl;
});
// name: Jackie
// last_name: Kay
// age: 24
```

The basic idea behind the magic is that the macro generates generic set and get functions for each member. It "stringizes" the field name given to the macro so it can be used as a compile-time string key, and uses the identifier name and type to get the member pointer for each field. Then it adapts the struct to a tuple of compile-time string key, member pointer accessor pairs.

Though that sounds fairly straightforward, the code is quite formidable and verbose. That's because there's a separate macro case for adapting a struct of specific sizes. For example, all structs with 1 member map to a particular macro, all structs with 2 members map to the next macro, etc.

The macro is actually generated from a less than 200-line [Ruby template](https://github.com/boostorg/hana/blob/master/include/boost/hana/detail/struct_macros.erb.hpp). You might not know Ruby, but you can get a taste of the pattern by looking at the template, which can be expanded for a maximum of n=62 (line breaks are my own):

```c++
#define BOOST_HANA_DEFINE_STRUCT(...) \
    BOOST_HANA_DEFINE_STRUCT_IMPL(BOOST_HANA_PP_NARG(__VA_ARGS__), __VA_ARGS__)

#define BOOST_HANA_DEFINE_STRUCT_IMPL(N, ...) \
    BOOST_HANA_PP_CONCAT(BOOST_HANA_DEFINE_STRUCT_IMPL_, N)(__VA_ARGS__)

<% (0..MAX_NUMBER_OF_MEMBERS).each do |n| %>
#define BOOST_HANA_DEFINE_STRUCT_IMPL_<%= n+1 %>(TYPE <%= (1..n).map { |i| ", m#{i}" }.join %>)       \
  <%= (1..n).map { |i| "BOOST_HANA_PP_DROP_BACK m#{i} BOOST_HANA_PP_BACK m#{i};" }.join(' ') %>       \
                                                                                                      \
  struct hana_accessors_impl {                                                                        \
    static constexpr auto apply() {                                                                   \
      struct member_names {                                                                           \
        static constexpr auto get() {                                                                 \
            return ::boost::hana::make_tuple(                                                         \
              <%= (1..n).map { |i| "BOOST_HANA_PP_STRINGIZE(BOOST_HANA_PP_BACK m#{i})" }.join(', ') %>\
            );                                                                                        \
        }                                                                                             \
      };                                                                                              \
      return ::boost::hana::make_tuple(                                                               \
        <%= (1..n).map { |i| "::boost::hana::make_pair(                                               \
            ::boost::hana::struct_detail::prepare_member_name<#{i-1}, member_names>(),                \
            ::boost::hana::struct_detail::member_ptr<                                                 \
                decltype(&TYPE::BOOST_HANA_PP_BACK m#{i}),                                            \
                &TYPE::BOOST_HANA_PP_BACK m#{i}>{})" }.join(', ') %>                                  \
      );                                                                                              \
    }                                                                                                 \
  }                                                                                                   \
```

Why did the authors of these libraries write these macros in the first place? Bragging rights, of course!

Actually, tuple-like access for structs is incredibly useful for generic programming. It allows you to perform operations on every member of a type, or a subset of members conforming to a particular concept, without having to know the specific names of each member. Again, this is a feature that you can't get with inheritance, templates, and concepts alone.

The major disadvantage of these macros is that to reflect on a type you either need to define it with `DEFINE_STRUCT`, or to refer to it and all of its introspectible members in the correct order with `ADAPT_STRUCT`. Though the solution requires less logical and code redundancy than code generation techniques, this last bit of hand-hacking wouldn't be required if we had language-level reflection.

### magic_get

The current pinnacle of POD introspection in C++14 is a library called [`magic_get`](https://github.com/apolukhin/magic_get) by Antony Polukhin. Unlike Fusion or Hana's approach, `magic_get` doesn't require calling a `DEFINE_STRUCT` or `ADAPT_STRUCT` macro for each struct we want to introspect. Instead, you can put an object of any POD type into the `magic_get` utility functions and access it like a tuple. For example:

```c++
Person author{"Jackie", "Kay", 24};
std::cout << "People are made up of " << boost::pfr::tuple_size<Person>::value << " things.\n"
std::cout << boost::pfr::get<0>(author) << " has a lot to say about reflection\n"!
// People are made up of 3 things.
// Jackie has a lot to say about reflection!
```

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

What's going on here? Antony is decomposing a flat struct of type T into a bunch of references named after consecutive letters in the alphabet. He then forwards those fields on to a tuple of references of his custom tuple type. This is the core mechanism for "converting" from a struct to a tuple. But since there's no way to destructure an arbitrary number of fields ("variadic structured bindings"?), this code can't be written in a generic fashion, and the library instead resorts to generating these specializations via a [Python script](https://github.com/apolukhin/magic_get/blob/develop/misc/generate_cpp17.py).

Again, if we had language-level reflection in C++, these code generation techniques would not be necessary.

### Function reflection: HippoMocks and tromploeil
TODO: do I want to cover this?

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

```python
soup.title.string
# u'The Dormouse's story'

soup.title.parent.name
# u'head'
```

This access will work for arbitrary XML tags (as long as the XML is well-formed).

Implementing such a library in C++ today is basically impossible, because it involves translating runtime values into member identifiers. If you're not yet convinced that reflection would be great for parser libraries, consider that the most powerful parser library in C++ today is Boost Spirit, which makes heavy use of the Boost Fusion reflection macro.

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

### Honorable mentions
The C# reflection system is somewhat similar to Java's: it allows type lookup by string identifiers and reifies types as runtime objects. It has utilities for getting and setting reflected fields, and even invoking reflected functions. It was designed with the use case of "runtime assemblies" in mind; that is, the ability to load dynamic shared libraries at runtime and use the reflection interface to reach into the API of the shared library. For more information I recommend [these](http://www.codeguru.com/csharp/csharp/cs_misc/reflection/article.php/c4257/An-Introduction-to-Reflection-in-C.htm) [articles](https://msdn.microsoft.com/en-gb/library/f7ykdhsy.aspx).

Go was designed to be a simpler and more "productive" C++ replacement. It has a few advantages over C++, like a bigger standard library, but its facilities for generics and metaprogramming are far cry from modern C++. This (now somewhat dated) article on [why Go doesn't need generics](https://appliedgo.net/generics/) might make you mad if you love generic programming (like me), but it also gives an introduction to interfaces and reflection, the stand-ins for language-level generics. The Go [`reflect`](https://golang.org/pkg/reflect/) package implements runtime reflection. It is essentially a utility for retrieving a runtime identifier for the concrete type stored in an interface. If you're curious about this, read about the Go [laws of reflection](https://blog.golang.org/laws-of-reflection).

Rust is a notable competitor to C++ in many areas. Like C++ it currently lacks language-level reflection support, but it is possible to hack an implementation using some combination of AST plugins, attributes, and macros.

## The road to standardization: reflexpr and operator$

The C++ ISO Standards Committee has a study group on reflection and metaprogramming, SG7, which is evaluating multiple reflection proposals targeting C++20.

The `reflexpr` proposal, by Matúš Chochlík, Axel Naumann, and David Sankel, introduces several "metaobjects" which are accessed by passing a type to the new `reflexpr` operator. I recommend ["Static Reflection in a Nutshell"](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0578r0.html) for a brief overview of the design and [Static reflection](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0194r3.html) for a longer overview. You can find instructions for building Matúš's fork of Clang which implements `reflexpr`, read his documentation, and explore his `mirror` utility library [here](http://matus-chochlik.github.io/mirror/doc/html/index.html).

Andrew Sutton and Herb Sutter wrote ["A design for static reflection"](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2017/p0590r0.pdf), which introduces the reflection operator, `$`, as a way of getting object metadata out of a class, namespace, etc. (Using `$` has been argued out of favor because it is common in legacy code, particularly in code generation and template systems which are not necessarily valid C++ but produce C++ sources.) You can explore Andrew Sutton's Clang fork implementing the proposal [here](https://github.com/asutton/clang-reflect).

The fundamental design difference between these two papers is whether the result of the reflection operator is a value or a type.

For example, here's a simple reflection example with the `reflexpr` paper:

```c++
namespace meta = std::meta;
using MetaPerson = reflexpr(Person);
std::cout << "A " << meta::get_base_name_v<MetaPerson> << " is made up of "
          << meta::get_size<MetaPerson> << " things.\n";
// A Person is made up of 3 things.
```

And here's how you would do the same thing using `operator$`:

```c++
namespace meta = cpp3k::meta::v1;
constexpr auto info = $Person;
std::cout << "A " << info.name() << " is made up of
          " << info.member_variables().size() << " things.\n";
```

The full discussion behind type-based metaprogramming vs. value-based metaprogramming could be another long blog post by itself. In fact, reflection is a use case for metaprogramming facilities, and the desire to standardize reflection in the language is making the design issues behind C++ metaprogramming more apparent. In the current state of C++ metaprogramming, it's often faster for the compiler to compute operations on pure types than it is to compute operations on constexpr values. Refer to the work and blog posts of Odin Holmes and the [benchmarks](http://metaben.ch/) comparing the Metal, Hana, and Kvasir MPL libraries. However, the syntax of value-based metaprogramming, like Boost Hana, is more intuitive and succinct than working entirely within template angle brackets and using declarations. It's also easier to jump from compile-time to runtime values in the value-based metaprogramming style.

In my opinion, the language design should go in the direction of value-based metaprogramming and compiler implementers should assess how compilation speeds of constexpr-heavy codebases can be improved. This would make metaprogramming far more accessible and practical to the average user. However, the standard cannot enforce requirements on compile times; this is entirely a quality of implementation issue, and I don't know enough about compilers to assess how much compile times could be improved--but I do know that compiler writers are some of the most performance-oriented programmers in the world.

Note also that the type-based reflection API could be wrapped into a value-based metaprogramming utility, a point that is explicitly highlighted in the "Static Reflection in a Nutshell" paper. Given the state of the language specification and compiler implementations today, it seems like a safe approach to provide a minimal type-based reflection facility, for those who want to remain in the land of pure types, and allow those who prefer the convenience of value-based metaprogramming to use a utility library. However, you could argue that this approach is not future-proof if the language is moving quickly away from old-style template metaprogramming. These points are hard to debate without concrete examples and developer experience, which is why I'm looking forward to more experimentation with the reference implementations of both reflection proposals.

Regardless of whether you prefer type-based metaprogramming or value-based metaprogramming, there are some metaprogramming features that need to be added to the C++ standard to make static reflection more palatable. A standardized representation constexpr string is needed for computations using the names of fields and types. C++17 gives us `if constexpr` and fold expressions with binary operators, but expanding constexpr control flow to constexpr for loops and more expressive and generic folding utilities would be extremely useful for the kind of work we'll want to do with static reflection. And Concepts are design prerequisite for both of the reflection papers, despite being in a TS and not in yet a first-class citizen of the language. If some of these points aren't totally clear to you yet, we'll try to demonstrate them by example in the next article in this series.

## Conclusion (for now)

I believe reflection is an indispensible tool for generic libraries and huge frameworks that deal with lots of different types and complex relationships between those types. Standardizing reflection in C++ will help make this feature more accessible to the average programmer and make C++ programmers more productive overall by strengthening the generic library ecosystem. Providing a zero-overhead reflection facility will also help C++ improve upon other general-purpose, mainstream programming languages that do offer reflection, like Java, and distinguish itself from competing languages without reflection facilities, like Rust.

Tune in next time for part 2 of this series, where I'll demonstrate what you can do with reflection in C++!
