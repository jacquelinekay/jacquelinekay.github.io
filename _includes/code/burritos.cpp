#include <boost/hana.hpp>
#include <iostream>
#include <tuple>

using grams = double;
using nutrition_tuple = std::tuple<grams, grams, grams, grams>;

namespace hana = boost::hana;

// Define foods in terms of their nutrients
template <typename F> struct food {
  food(grams g)
      : carbs(F::carbs_per_g * g), protein(F::protein_per_g * g),
        fat(F::fat_per_g * g), sodium(F::sodium_per_g * g) {}

  food(nutrition_tuple const &t)
      : carbs(std::get<0>(t)), protein(std::get<1>(t)), fat(std::get<2>(t)),
        sodium(std::get<3>(t)) {}

  template <typename G>
  food(food<G> const &g)
      : carbs(g.carbs), protein(g.protein), fat(g.fat), sodium(g.sodium) {}

  std::ostream &print_nutrients(std::ostream &os) const {
    os << "Nutrition facts:" << std::endl;
    os << "Carbohydrates: " << carbs << "g" << std::endl;
    os << "Protein: " << protein << "g" << std::endl;
    os << "Fat: " << fat << "g" << std::endl;
    os << "Sodium: " << sodium << "g" << std::endl;
    return os;
  }

  grams carbs;
  grams protein;
  grams fat;
  grams sodium;
};

// Combine the nutritional content of two foods.
template <typename F, typename G>
auto operator+(food<F> const &f, food<G> const &g) {
  return nutrition_tuple(f.carbs + g.carbs, f.protein + g.protein,
                         f.fat + g.fat, f.sodium + g.sodium);
}

template <typename F>
auto operator+(food<F> const &f, nutrition_tuple const &g) {
  return nutrition_tuple(f.carbs + std::get<0>(g), f.protein + std::get<1>(g),
                         f.fat + std::get<2>(g), f.sodium + std::get<3>(g));
}

template <typename F>
auto operator+(nutrition_tuple const &g, food<F> const &f) {
  return nutrition_tuple(f.carbs + std::get<0>(g), f.protein + std::get<1>(g),
                         f.fat + std::get<2>(g), f.sodium + std::get<3>(g));
}

template <size_t... i>
auto sum(std::index_sequence<i...>, nutrition_tuple const &f,
         nutrition_tuple const &g) {
  return nutrition_tuple((std::get<i>(f) + std::get<i>(g))...);
}

auto operator+(nutrition_tuple const &f, nutrition_tuple const &g) {
  return sum(std::make_index_sequence<4>{}, f, g);
}

// Calculate and assign nutritional content from a tuple of foods
template <size_t... i, typename... Fillings>
auto calculate_nutrients_helper(std::index_sequence<i...>,
                                std::tuple<Fillings...> const &fillings) {
  return (nutrition_tuple(0.0, 0.0, 0.0, 0.0) + ... + std::get<i>(fillings));
}

template <typename... Fillings>
auto calculate_nutrients(std::tuple<Fillings...> const &fillings) {
  return calculate_nutrients_helper(std::index_sequence_for<Fillings...>{},
                                    fillings);
}

template <typename T> struct is_food {
  using D = std::decay_t<T>;
  static const bool value = std::is_base_of_v<food<D>, D>;
};
template <typename T> constexpr bool is_food_v = is_food<T>::value;

// Each constructor takes a float specifying how many grams of the food we want.
struct beef : public food<beef> {
  beef(grams g) : food<beef>(g) {}
  constexpr static grams carbs_per_g = 0.0;
  constexpr static grams protein_per_g = 0.26;
  constexpr static grams fat_per_g = 0.015;
  constexpr static grams sodium_per_g = 0.0007;
};

struct chicken : public food<chicken> {
  chicken(grams g) : food<chicken>(g) {}
  constexpr static grams carbs_per_g = 0.0;
  constexpr static grams protein_per_g = 0.27;
  constexpr static grams fat_per_g = 0.014;
  constexpr static grams sodium_per_g = 0.0008;
};

struct rice : public food<rice> {
  rice(grams g) : food<rice>(g) {}
  constexpr static grams carbs_per_g = 0.28;
  constexpr static grams protein_per_g = 0.027;
  constexpr static grams fat_per_g = 0.03;
  constexpr static grams sodium_per_g = 0.0;
};

struct beans : public food<beans> {
  beans(grams g) : food<beans>(g) {}
  constexpr static grams carbs_per_g = 0.63;
  constexpr static grams protein_per_g = 0.21;
  constexpr static grams fat_per_g = 0.09;
  constexpr static grams sodium_per_g = 0.0001;
};

// Conceptually, a burrito is a tortilla wrapping zero or more fillings.
// A burrito with zero (empty tortilla) is the identity burrito.
// A burrito can only accept fillings from the Food category.
// A burrito is also a kind of food.
// A burrito can hold other burritos.
template <typename... Fillings>
struct burrito : public food<burrito<Fillings...>> {
  using food_type = food<burrito<Fillings...>>;
  burrito(Fillings &&... f)
      : food_type(calculate_nutrients(
            std::make_tuple(std::forward<Fillings>(f)...))),
        fillings(std::make_tuple(std::forward<Fillings>(f)...)) {}

  burrito(std::tuple<Fillings...> &&t)
      : food_type(calculate_nutrients(t)),
        fillings(std::forward<std::tuple<Fillings...>>(t)) {}

  // Conservation of matter: to retrieve the fillings of the burrito, you must
  // move them out.
  auto unwrap_fillings() { return std::move(fillings); }

  static constexpr size_t n_fillings = sizeof...(Fillings);

  // Print the nutritional contents of this food.
  std::ostream &print_nutrients(std::ostream &os) const {
    return food_type::print_nutrients(os);
  }

  std::tuple<Fillings...> fillings;
};

template <typename F>
std::ostream &operator<<(std::ostream &os, const food<F> &f) {
  return f.print_nutrients(os);
}

// Utilities for cooking new burritos
template <typename... Fs> static auto make_burrito(Fs &&... fs) {
  return burrito<Fs...>(std::forward<Fs>(fs)...);
}

template <typename... Fs> static auto make_burrito(std::tuple<Fs...> &&t) {
  return burrito<Fs...>(std::forward<std::tuple<Fs...>>(t));
}

struct burrito_tag {};

template <typename... T> struct hana::tag_of<burrito<T...>> {
  using type = burrito_tag;
};

// Free function form of unwrap_fillings, for variadic expansion
template <typename Burrito> static auto unwrap_fillings(Burrito &&b) {
  return b.unwrap_fillings();
}

// Functor
template <> struct hana::transform_impl<burrito_tag> {
  template <std::size_t... i, typename F, typename... Fs>
  static constexpr auto transform_helper(std::index_sequence<i...>, F &&f,
                                         std::tuple<Fs...> &&t) {
    return make_burrito(f(std::get<i>(t))...);
  }

  // Pull out the burrito's contents, apply the function
  // over the contents, and wrap it into a new burrito.
  template <typename F, typename... Fs>
  static constexpr auto apply(burrito<Fs...> &&b, F &&f) {
    return transform_helper(std::index_sequence_for<Fs...>{},
                            std::forward<F>(f), unwrap_fillings(b));
  }
};

// Applicative
template <> struct hana::lift_impl<burrito_tag> {
  template <typename X> static constexpr auto apply(X &&x) {
    // Wrap up the ingredients into a tortilla to make a burrito.
    return burrito<std::decay_t<X>>(std::forward<X>(x));
  }
};

template <> struct hana::ap_impl<burrito_tag> {
  template <typename F, typename X>
  static constexpr decltype(auto) apply(F &&f, X &&x) {
    return make_burrito(f(std::forward<X>(x)));
  }
};

// Monad
template <> struct hana::flatten_impl<burrito_tag> {
  template <typename... Burritos>
  static constexpr decltype(auto) flatten_helper(Burritos &&... bs) {
    return std::tuple_cat(unwrap_fillings(bs)...);
  }

  template <typename... Xs>
  static constexpr decltype(auto) apply(burrito<Xs...> &&xs) {
    // xs is a burrito containing other burritos.
    // We want to unwrap the tortillas and merge the ingredients into one
    // burrito with one tortilla.
    return make_burrito(flatten_helper(std::forward<burrito<Xs...>>(xs)));
  }
};

int main(int argc, char **argv) {
  // Some valid operations over food types
  auto fry = [](auto &&f) {
    if constexpr(is_food_v<decltype(f)>) {
      f.fat += 14;
      return f;
    } else {
      return hana::nothing;
    }
  };

  auto salt = [](auto &&f) {
    if constexpr(is_food_v<decltype(f)>) {
      f.sodium += 0.002;
      return f;
    } else {
      return hana::nothing;
    }
  };

  {
    auto bb = make_burrito(beans(50.0), rice(100.0), beef(100.0));
    std::cout << "Beef burrito: " << std::endl << bb << std::endl;

    // Check that the burrito type conforms to the required concepts.
    static_assert(hana::Functor<decltype(bb)>::value,
                  "Burrito instance does not model Functor concept.");
    static_assert(hana::Applicative<decltype(bb)>::value,
                  "Burrito instance does not model Applicative concept.");
    static_assert(hana::Monad<decltype(bb)>::value,
                  "Burrito instance does not model Monad concept.");

    auto cb = make_burrito(beans(50), rice(100), chicken(100));
    std::cout << "Chicken burrito: \n" << cb << std::endl;

    auto monster_burrito = make_burrito(std::move(cb), std::move(bb));
    std::cout << "Nested monster burrito: \n" << monster_burrito << std::endl;

    // Flatten burrito levels.
    auto merged_burrito = hana::flatten(std::move(monster_burrito));
    std::cout << "Merged burrito: \n" << merged_burrito << std::endl;

    // You can fry each individual ingredient of a burrito.
    auto fried_bb = hana::transform(std::move(merged_burrito), fry);
    std::cout << "Fried burrito: \n" << fried_bb << std::endl;

    // Or you can fry an entire burrito.
    auto chimichanga = fry(fried_bb);
    std::cout << "Chimichanga: \n" << chimichanga << std::endl;
  }

  {
    auto bb = make_burrito(beans(50), rice(100), beef(50));
    // You can selectively apply an operation to the ingredients of the burrito.
    auto needs_salt = [](auto&& f){
      if constexpr(is_food_v<decltype(f)>) {
        return f.sodium < 0.0002;
      } else {
        // Can't apply salt to something that isn't food
        return false;
      }
    };
    auto salted_bb = hana::adjust_if(std::move(bb), needs_salt, salt);
    std::cout << "With salted rice and beans: \n" << salted_bb << std::endl;

    // Chain operations on a burrito.
    auto cb = make_burrito(beans(50), rice(100), chicken(50));
    auto burrito_pipeline = hana::monadic_compose(fry, salt);
    auto combo_burrito = burrito_pipeline(std::move(cb));

    std::cout << "Salty fried chicken burrito:\n" << combo_burrito << std::endl;
    std::cout << "Heart attack in a tortilla:\n"
              << burrito_pipeline(chimichanga) << std::endl;

    std::string bar = "A bar is not edible.";
    auto result = burrito_pipeline(bar);
    static_assert(result == hana::nothing);
  }

  return 0;
}
