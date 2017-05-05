---
layout: post
title: Fun with Reflection in C++
---

In [my previous post]({{site.baseurl}}/2017/04/13/reflection1.html), we learned about the current and future state of reflection in C++. But I left a few questions unanswered. Indeed, you may still be wondering why I care so much about reflection and if it has any useful applications for the average programmer. In this post, I'll try to answer that question with real code examples using the two reference implementations of C++ reflection. I'll explore the strengths of the two implementations, as well as the major limitations. These examples make heavy use of metaprogramming and C++17 features, so if you find yourself in unfamiliar territory while reading the code, I suggest supplementing this article with other resources.

When I refer to the `reflexpr` implementation, I'm talking about [Matúš Chochlík's fork of Clang](http://matus-chochlik.github.io/mirror/doc/html/index.html) which implements P1094, by Chochlík, Axel Naumann, and David Sankel.

When I refer to `cpp3k`, I'm talking about [Andrew Sutton's fork of Clang](https://github.com/asutton/clang-reflect) which implements P0590R0, by Sutton and Herb Sutter.

Disclaimer: these reference implementations do not represent the final state of the reflection proposals. They are simply prototypes and the API that may end up in the language is highly subject to change!

# Generating comparison operators

Have you ever written a tedious equality operator that checked for the equality of every member of a regular type? Have you ever written a code generation tool for generator such a function? You're not the only one. In fact, this is such a problem that someone wrote a standards proposal for adding default comparison operators for regular types to the language ([N3950](http://www.open-std.org/jtc1/sc22/wg21/docs/papers/2014/n3950.html)). (TODO: show the one Herb proposed too?)

It turns out that with reflection, you can write a generic equality operator for any type composed of equality-comparable members!

## reflexpr

```c++ {% include utils/includelines filename='code/reflection/reflexpr/comparisons.hpp' start=12 count=25 %}```

Note that `metap` is simply my own namespace that provides some metaprogramming utilities.

This implementation uses the [detection idiom](http://en.cppreference.com/w/cpp/experimental/is_detected) to check if the type T has a valid equality operator. If it does, return the result of that equality comparison for the two input objects. Otherwise, we recursively call "equal" on each member of T. If the type is neither equality comparable or a record (something with members), then that means we can't compare T for equality.

The trickiest part of this example is the use of `meta::unpack_sequence_t` and fold expressions. To get metainfo for each member of T, we call `get_data_members` on `reflexpr(T)`, the metainfo for T. This returns what the proposal currently calls a meta-object sequence, which must be unpacked using `unpack_sequence_t`. `unpack_sequence_t` takes the meta-object sequence, which is represented as a type, and a struct which is templated on a parameter pack of types, and returns the instantiation of the templated struct with the object sequence as the parameter pack.

This may seem weird to you, but it's also the only way the authors of this proposal could see to transfer a compile-time sequence of types to variadic template arguments. Once we have the metainfo for each member, we can compute equality for each member elegantly using a fold expression on the boolean "and" operator. Within that fold expression, we access members by using the member pointer provided by that metainfo (`a.*p` where `p = meta::get_pointer<MetaMembers>::value`).

One thing that's a bit odd about this example is that we have to forward-declare the `equal` operator so that we can call it within the `apply` function which applies the fold expression, because `equal` also refers to `compare_fold::apply`. Another syntactic nitpick is that we have to call `get_pointer<MetaMembers>::value` twice within the fold expression so that MetaMembers can be unpacked; if we stored the result in a value, the compiler would complain about an unexpanded parameter pack.

## cpp3k

```c++ {% include utils/includelines filename='code/reflection/cpp3k/comparisons.hpp' start=12 count=14 %}```

The basic idea of this example is the same as the previous one. You may find it shorter and more elegant due to the use of value semantics instead of type semantics for accessing metainformation. The most important difference is the use of `meta::for_each` instead of `unpack_sequence_t`. `meta::for_each` implements a for loop over heterogeneous types. It allows us to write the equality comparison as a lambda function. This has the advantage that it requires less syntactic overhead than defining a struct, but it requires us to capture our inputs into the lambda, which could be annoying if there's a lot of state that needs to be shared. More importantly, it requires us to initialize the result and capture it. In this example, it's trivially known what the initial state of the comparison should be, but there could be cases where the initial state is not known. `unpack_sequence_t` allows us to directly access the result of the operation we wrote over the members.

Also, the `reflexpr` implementation also offers an implementation of `for_each` with a similar interface: it accepts an object sequence like the result of `get_data_members_m` as a type parameter and a function object and applies the function over the sequence. Thus you can achieve a very similar example as what I showed here with `reflexpr`. I just wanted to show the two different styles of iterating over members.

# Serialization and deserialization

If you want to save data to hard disk or share data across different processes or machines, you need serialization and deserialization. Converting information between a structured representation that can be understood by a program and an unstructured storage format is a fundamentally important problem for practical applications.

In the C++ world, libraries like Boost Serialization, Cereal, yaml-cpp, and Niels Lohmann's JSON parser are indispensible for this problem. These libraries often include adaptors for standard library containers like `vector` and `map`. However, they do not have true generic power: for your custom data types, you have to specify the data layout for serialization, e.g. which data fields in the serialization format go into which member fields of the type. Reflection makes it possible to infer that data layout automatically from the definition of the C++ struct.

For certain data types, you might want more control over which fields are a part of the serialization format. In the case of the standard library containers, you probably don't want to serialize members used for bookkeeping or metadata that exposes implementation details. But for the case of POD types which represent configuration maps or protocol messages, generic serialization saves us the pain of writing and maintaing a lot of repetitive boilerplate code.

In this example we'll write a simple JSON serializer. If you're closely following reflection and metaprogramming, you may recall that Louis Dionne's keynote at Meeting C++ 2016 showed an example of [JSON serialization](https://github.com/ldionne/meetingcpp-2016/blob/gh-pages/code/reflection.cpp) using reflection and value semantic metaprogramming. But he did not show JSON deserialization, which requires some extra thought. Since I'm not using Boost Spirit here, the task requires a lot of token parsing boilerplate that I'll omit for the sake of brevity, but you can always see the full example on Github.

## reflexpr

The basic idea behind serialization is straightforward: like the equality operator example, we'll apply a serialization operation over the members of a struct if they are serializable primitives (numeric types or strings) and recursively call the serialize function on the member if it is a Record.

We'll use `if constexpr` and a mix of type traits and the detection idiom for the "base cases". `stringable` detects if the type has a `std::to_string` operator. `iterable` detects, roughly, if a type can be used in a range-based for loop, like a vector or array (although right now it's not a bulletproof implementation). This will map to a JSON array of the values.

```c++ {% include utils/includelines filename='code/reflection/reflexpr/reflser.hpp' start=195 count=23 %}```

To handle the case where T is a POD type, we'll recursively apply the serialize function over the members of T using reflection. `get_base_name_v` gets the name of the member from the metainfo. We'll use this as the key name in the JSON object.

```c++ {% include utils/includelines filename='code/reflection/reflexpr/reflser.hpp' start=226 count=11 %}```

Deserialization is where it gets more interesting. I'll skip the part of the code that deals with primitive types as well as the parser boilerplate, and show the parts related to reflection.

First, we count the colons and commas in the outermost scope of the JSON object that we are mapping to our member, and return an error if the number of colons mismatched (since that represents a key-value mapping):

```c++ {% include utils/includelines filename='code/reflection/reflexpr/reflser.hpp' start=368 count=3 %}```

For every key, value pair in the JSON object, we'll find the string representing the key and the string representing the value. Then, we need to match the key string in the set of possible member names for the struct we are deserializing JSON into. Because the key string is not known at compile time, we will have to pay some runtime cost to do this lookup. For now, we'll simply loop over the members of the struct and compare the runtime string key to the name of each member.

```c++ {% include utils/includelines filename='code/reflection/reflexpr/reflser.hpp' start=388 count=13 %}```

As you can see here, if the key matches the name of the member, we'll grab the type of the member from the metainfo, and retrieve the member pointer corresponding to that member.

I've added a couple of utilities here to make this code more readable and brief.

`sl::string_constant` is a custom constexpr string class inspired by a pattern that was probably created by [Michael Park](https://github.com/mpark), popularized by Boost Hana, and recently generalized to arbitrary values by [ubsan](https://github.com/ubsan/typeval/). This is mostly for the convenience of faking the syntax of passing constexpr value to a constexpr function.

`get_member_pointer` is a utility that maps the constexpr string name of a member to the member index, and then retrieves the member pointer corresponding to that member.

```c++ {% include utils/includelines filename='code/reflection/reflexpr/refl_utilities.hpp' start=73 count=9 %}```

The implementation of `index_of_member` is also a bit funny. We compute a fold expression over each member of the struct again, comparing the constexpr string name to the name of the member. If the name matches, we add the index of that member to the result, otherwise we add zero.

```c++ {% include utils/includelines filename='code/reflection/reflexpr/refl_utilities.hpp' start=58 count=14 %}```

In this post, I'm following the "implement now, benchmark later" philosophy. If you're obsessed with performance and the the rather naive runtime-determined member lookup presented here bothered you, don't worry. You might be able to imagine how we can improve O(n) runtime string comparisons and O(n) compile-time string comparisons, where n is the number of members of the struct. We'll analyze the performance and see how we can do better... in the next blog post in my reflection series!

## cpp3k

The `cpp3k` version of the same code has a similar structure, but is overall cleaner and more terse--to reiterate the point Louis made in his aforementioned keynote. This is how we loop over members to serialize them:

```c++ {% include utils/includelines filename='code/reflection/cpp3k/reflser.hpp' start=224 count=10 %}```

One notable issue with the current state of this implementation is that I couldn't find a good "type trait" equivalent to the `Record<T>` concept, which simply returns true if T is a type that contains members. I don't think this is an intentional emission from the `cpp3k` implementation, since this kind of introspectability is key for the kind of generic programming that reflection allows, and I have hope that Herb and Andrew understand that.

Anyway, I went ahead and implemented a type trait using the detection idiom so that I could switch on this concept using `if constexpr`. This is not a great implementation since it could easily be faked by another interface, but it gets the job done for this example:

```c++ {% include utils/includelines filename='code/reflection/cpp3k/refl_utilities.hpp' start=18 count=5 %}```

The deserialization code is much cleaner and requires fewer helper functions because of the value semantics of this API: we can simply access the member pointer directly from the metainfo. (We are still matching the runtime string to a member metainfo by looping over each member.)

```c++ {% include utils/includelines filename='code/reflection/cpp3k/reflser.hpp' start=389 count=10 %}```

# Program options and member annotation

Let's start with a common problem in C++: you want to map `int argc, char** argv` from an incredibly primitive C-style array to a set of program configuration options, which you've encapsulated as a struct that gets passed around to initialize your application. You could write an "if" statement for each flag you want to recognize and manually stuff the options struct with the parsed values. Or, you could write a generic parse function that changes its behavior based on the layout of the options struct and some compile-time configuration options.

This example may remind you of a commonly used solution for this problem: Boost Program Options. Since this is just an example and not a fully-fledged library, it offers a much more limited interface, but my thought is that static reflection and metaprogramming can be used to implement a similar library with less runtime cost.

Of course, we will need to add some annotations to our program configuration, such as which flags specify which options.

Recall that C++11 added attributes to the language: annotations using `[[double bracket]]` syntax that may change how the compiler treats a function, a declaration, an expression, or pretty much anything. Ideally, we could simply add our own attributes and reflect on them (this is known as "user-defined attributes" in standard proposal-land).

Unfortunately the reflection proposals and their implementations don't have user-defined attributes baked in. However, I'm going to show an implemention of annotated members for program options using reflection, some Boost Hana utilities and--you guessed it--macros.

The basic idea is that, within a struct, we call a macro REFLOPT_OPTION to define a member given its type, name, the flags which map to the member on the command line, and an optional help string describing what the option does.

```c++
struct ProgramOptions {
  REFLOPT_OPTION(std::string, filename, "--filename");
  REFLOPT_OPTION(int, iterations, "--iterations", "-i",
    "Number of times to run the algorithm.")
  REFLOPT_OPTION(bool, help, "--help", "-h", "Print help and exit.")
};
```

The `REFLOPT_OPTION` macro defines a member with the given type and name. It also defines a member of templated type `Option` that holds constexpr strings with the option-related metadata.

(Many thanks to [Vittorio Romeo](http://www.vittorioromeo.info) for inspiring me to write this example and for feedback on the syntax for annotating structs.)

We'll define a `parse` function that detects if the input struct contains members which specialize `Option` and generates parser code based on the option metadata.

The `parse` function instantiates a helper OptionsMap type. At compile time, OptionsMap generates two runtime functions, `contains` and `set` based on the layout of the struct which holds our program options. `contains` will check if a given argument flag is valid based on the Option metadata. `set` will set the field in the program options struct corresponding to the given flag to the value specified on the command line. `parse`'s return type is `std::optional`; it returns the field program options struct if the given argument vector was valid or nullopt if it couldn't figure out how to prase the arguments. (This should probably be an `expected` instead of an `optional`, but since the proposed interface of `optional` is simpler we'll use that for the sake of demonstration).

```c++
  template<typename OptionsStruct>
  optional_t<OptionsStruct> parse(int argc, char** const argv) {
    OptionsStruct options;
    for (int i = 1; i < argc; i += 2) {
      if (OptionsMap<OptionsStruct>::contains(argv[i])) {
          OptionsMap<OptionsStruct>::set(options, argv[i], argv[i + 1]);
      } else {
        // unknown prefix found
        return std::experimental::nullopt;
      }
    }
    return options;
  }
```

`OptionsMap<T>` scans over all the members of `T`. For every member which is a specialization of `Option`, we look at hte flags mapping to that option and collect a compile-time map which associates the flag strings with the reflection metainfo of the member representing the program option. In the implementation of `contains`, we'll take the runtime string input and compare it against the compile-time string keys of this map for a match. In the implementation of `set`, we'll take the metainfo value associated with the compile-time key that matches the given flag, and retrieve the type of the member, so that we can convert the string value to the right type using `boost::lexical_cast`, and the member pointer, so that we can set the value in the program options struct.

## reflexpr

One key helper function we need for this example is `get_metainfo_for`, which retrieves the metainfo for a member given a compile-time string representing its name. This requires some boilerplate since associative access of members based on the name of the identifier is not a part of the proposal, and because the constexpr string representation chosen by the proposal cannot be used as a key in a Hana compile-time map.

```c++ {% include utils/includelines filename='code/reflection/reflexpr/reflopt.hpp' start=41 count=26 %}```

(If you have thoughts on how to clean up this section of the code and/or the below `cpp3k` implementation, pull requests or comments are welcome! :])

In terms of syntactic overhead and code aesthetics, the one place where the raw `reflexpr` API has an advantage over `cpp3k` is when you want to directly grab a type and use it in a template (angle-bracket) context. You can see this in the implementation of `set`:

```c++ {% include utils/includelines filename='code/reflection/reflexpr/reflopt.hpp' start=128 count=14 %}```

As we'll see, the cpp3k implementation will require a little more to unwrap a type from a value to be used in the same way.

## cpp3k

The implementation of `get_metainfo_for` is slightly nicer than above, but not by much.

```c++ {% include utils/includelines filename='code/reflection/cpp3k/reflopt.hpp' start=43 count=23 %}```

Notice that after getting the index corresponding to the identifier we use a new utility from `cpp3k`: `cget`, the constexpr free function that accesses the heterogenous sequence container which results from `$T.member_variables()`.

In the implementation of `set`, we need to retrieve the member type from the metainfo.

Now, this may be another bug in the `cpp3k` implementation, but I couldn't find a way to gracefully retrieve the type of the member from the metainfo value. Ideally we could do this:

```c++
auto use_metainfo = [](auto&& metainfo, const char* str) {
  using MemberT = std::decay_t<decltype(metainfo)>::type;
  return boost::lexical_cast<MemberT>(str);
};
```

But trying to retrieve the type like this didn't compile, so I had to write an `unreflect_type` helper function to do this.

```c++ {% include utils/includelines filename='code/reflection/cpp3k/reflopt.hpp' start=119 count=13 %}```

The implementation of `unreflect_type` is not pretty, which makes me think the lack of type retrieval is an unintentional omission:

```c++ {% include utils/includelines filename='code/reflection/cpp3k/refl_utilities.hpp' start=36 count=8 %}```

# What's missing?

Reading this blog post, you might feel as if there's something missing. Maybe it was the code smell coming from the macro used for member annotations in the program options example. Or maybe you just aren't impressed by the examples so far.

The reflection proposals we've looked at do not extend the code generation features of the language. Unfortunately, while brainstorming what I could do with reflection, I had a lot of ideas for utilities that required the ability to add new type members based on the metainformation resulting from reflection, such as:

- Auto-generation of mock classes. Frameworks like the impressive [trompeloeil](https://github.com/rollbear/trompeloeil) library require code duplication of the mocked interface. Reflection as it is currently proposed doesn't solve this problem.
- Function instrumentation: this requires essentially the same features needed for mocking. The idea is to automatically a class with the same interface as the input class that counts the number of calls to a function or other statistics for each function in a class.
- Generating code for "virtual concepts"/"interfaces"/"Rust-style traits". See also [dyno](https://github.com/ldionne/dyno).

Actually, I didn't include any function reflection examples, even though it is implemented in the `cpp3k` fork, because the most interesting and useful applications of function reflection require this missing feature! Introspecting on functions may give us valuable information about overload sets which would save a lot of boilerplate metaprogramming for certain generic libraries, but I didn't see a compelling and immediately obvious application of function reflection for the "everyday programmer".

TODO: example of reflection on overload sets?

At this point, you might be saying "Jackie, if you have so many opinions about this feature, why don't **you** write a proposal about it?" I'm just one person who has thought about the issue of type synthesis or identifier modification--and some of the others have many, many more years of C++ and software architecture development than I do! Herb Sutter's upcoming proposal on metaclasses is a promising way forward to make reflection and more generic programming as powerful as it needs to be. Out of respect for Herb I'll refrain from saying more until he's officially released this work.

(For the curious reader: the very first thing I tried to do with the `reflexpr` fork was making a ["type synthesis"](https://github.com/jacquelinekay/reflection_experiments/blob/master/include/reflection_experiments/reflexpr/type_synthesis.hpp) example which included metafunctions for adding and removing members using constexpr strings. However, I haven't polished this due to my realization that without language-level support for using reflection info in identifiers and standardization of more metaprogramming utilities like constexpr strings and heterogenous data structures and algorithms, the interface for such a library is, in my opinion, unusable.)

# Conclusion

In the third and final part of this series, we'll take a look at the performance implications of reflection as it's currently proposed--specifically, the metaprogramming techniques needed to make reflection useful.
