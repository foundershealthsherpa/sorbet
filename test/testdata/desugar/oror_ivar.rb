# typed: strict

module Config
  extend T::Sig

  sig {returns(T::Boolean)}
  def self.expensively_compute_boolean; false; end

  sig {returns(T::Array[String])}
  def self.supported_methods
    @supported_methods ||= T.let(['fast', 'slow', 'special'].uniq.freeze, T.nilable(T::Array[String]))
    T.reveal_type(@supported_methods) # error: Revealed type: `T::Array[String]`
    T.must(@supported_methods) # error: `T.must` called on `T::Array[String]`, which is never `nil`
  end

  sig {returns(String)}
  def self.initialized_to_nilable
    @initialized_to_nilable ||= T.let(nil, T.nilable(String))
  end # error: Expected `String` but found `T.nilable(String)` for method result type

  sig {returns(T::Boolean)}
  def self.lazy_boolean
    # This is dangerous, but it's dangerous in normal Ruby, too. (The `||`
    # condition sees that the value is `false` and re-runs the ivar
    # initialization.) Maybe a future Sorbet change will detect cases like this
    # and provide a warning.
    #
    # In any case, the solution, regardless of whether Sorbet is in use or not,
    # is to use `if defined?(@lazy_boolean)` (or `if @lazy_boolean.nil?` if you
    # don't care about uninitialized variable use warnings) instead of `||=`.
    @lazy_boolean ||= T.let(expensively_compute_boolean, T.nilable(T::Boolean))
  end

  sig {returns(Integer)}
  def self.suggest_t_let
    @suggest_t_let ||= ''
  # ^^^^^^^^^^^^^^ error: The instance variable `@suggest_t_let` must be declared using `T.let` when specifying `# typed: strict`
  # ^^^^^^^^^^^^^^ error: Use of undeclared variable `@suggest_t_let`
  end

  sig {returns(Integer)}
  def self.accidentally_untyped
    @accidentally_untyped ||= T.let(T.unsafe(nil), T.nilable(String))
    T.must(@accidentally_untyped) # error: `T.must` called on `T.untyped`, which is redundant
  end

  sig {void}
  def self.main
    # Still shows as typed in other methods
    T.reveal_type(@accidentally_untyped) # error: `T.nilable(String)`
  end
end
