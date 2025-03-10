/*
 * Copyright (c) 2018-2022, Andreas Kling <kling@serenityos.org>
 * Copyright (c) 2020-2021, the SerenityOS developers.
 * Copyright (c) 2021-2023, Sam Atkins <atkinssj@serenityos.org>
 * Copyright (c) 2021, Tobias Christiansen <tobyase@serenityos.org>
 * Copyright (c) 2022, MacDue <macdue@dueutil.tech>
 *
 * SPDX-License-Identifier: BSD-2-Clause
 */

#include <AK/CharacterTypes.h>
#include <AK/Debug.h>
#include <AK/GenericLexer.h>
#include <AK/SourceLocation.h>
#include <LibWeb/CSS/CSSFontFaceRule.h>
#include <LibWeb/CSS/CSSImportRule.h>
#include <LibWeb/CSS/CSSKeyframeRule.h>
#include <LibWeb/CSS/CSSKeyframesRule.h>
#include <LibWeb/CSS/CSSMediaRule.h>
#include <LibWeb/CSS/CSSNamespaceRule.h>
#include <LibWeb/CSS/CSSStyleDeclaration.h>
#include <LibWeb/CSS/CSSStyleRule.h>
#include <LibWeb/CSS/CSSStyleSheet.h>
#include <LibWeb/CSS/CSSSupportsRule.h>
#include <LibWeb/CSS/CalculatedOr.h>
#include <LibWeb/CSS/EdgeRect.h>
#include <LibWeb/CSS/MediaList.h>
#include <LibWeb/CSS/Parser/Block.h>
#include <LibWeb/CSS/Parser/ComponentValue.h>
#include <LibWeb/CSS/Parser/DeclarationOrAtRule.h>
#include <LibWeb/CSS/Parser/Function.h>
#include <LibWeb/CSS/Parser/Parser.h>
#include <LibWeb/CSS/Parser/Rule.h>
#include <LibWeb/CSS/Selector.h>
#include <LibWeb/CSS/StyleValue.h>
#include <LibWeb/CSS/StyleValues/AngleStyleValue.h>
#include <LibWeb/CSS/StyleValues/BackgroundRepeatStyleValue.h>
#include <LibWeb/CSS/StyleValues/BackgroundSizeStyleValue.h>
#include <LibWeb/CSS/StyleValues/BackgroundStyleValue.h>
#include <LibWeb/CSS/StyleValues/BorderRadiusShorthandStyleValue.h>
#include <LibWeb/CSS/StyleValues/BorderRadiusStyleValue.h>
#include <LibWeb/CSS/StyleValues/BorderStyleValue.h>
#include <LibWeb/CSS/StyleValues/ColorStyleValue.h>
#include <LibWeb/CSS/StyleValues/CompositeStyleValue.h>
#include <LibWeb/CSS/StyleValues/ContentStyleValue.h>
#include <LibWeb/CSS/StyleValues/CustomIdentStyleValue.h>
#include <LibWeb/CSS/StyleValues/DisplayStyleValue.h>
#include <LibWeb/CSS/StyleValues/EasingStyleValue.h>
#include <LibWeb/CSS/StyleValues/EdgeStyleValue.h>
#include <LibWeb/CSS/StyleValues/FilterValueListStyleValue.h>
#include <LibWeb/CSS/StyleValues/FlexFlowStyleValue.h>
#include <LibWeb/CSS/StyleValues/FlexStyleValue.h>
#include <LibWeb/CSS/StyleValues/FontStyleValue.h>
#include <LibWeb/CSS/StyleValues/FrequencyStyleValue.h>
#include <LibWeb/CSS/StyleValues/GridAreaShorthandStyleValue.h>
#include <LibWeb/CSS/StyleValues/GridAutoFlowStyleValue.h>
#include <LibWeb/CSS/StyleValues/GridTemplateAreaStyleValue.h>
#include <LibWeb/CSS/StyleValues/GridTrackPlacementShorthandStyleValue.h>
#include <LibWeb/CSS/StyleValues/GridTrackPlacementStyleValue.h>
#include <LibWeb/CSS/StyleValues/GridTrackSizeListShorthandStyleValue.h>
#include <LibWeb/CSS/StyleValues/GridTrackSizeListStyleValue.h>
#include <LibWeb/CSS/StyleValues/IdentifierStyleValue.h>
#include <LibWeb/CSS/StyleValues/ImageStyleValue.h>
#include <LibWeb/CSS/StyleValues/InheritStyleValue.h>
#include <LibWeb/CSS/StyleValues/InitialStyleValue.h>
#include <LibWeb/CSS/StyleValues/IntegerStyleValue.h>
#include <LibWeb/CSS/StyleValues/LengthStyleValue.h>
#include <LibWeb/CSS/StyleValues/ListStyleStyleValue.h>
#include <LibWeb/CSS/StyleValues/NumberStyleValue.h>
#include <LibWeb/CSS/StyleValues/OverflowStyleValue.h>
#include <LibWeb/CSS/StyleValues/PercentageStyleValue.h>
#include <LibWeb/CSS/StyleValues/PlaceContentStyleValue.h>
#include <LibWeb/CSS/StyleValues/PlaceItemsStyleValue.h>
#include <LibWeb/CSS/StyleValues/PlaceSelfStyleValue.h>
#include <LibWeb/CSS/StyleValues/PositionStyleValue.h>
#include <LibWeb/CSS/StyleValues/RatioStyleValue.h>
#include <LibWeb/CSS/StyleValues/RectStyleValue.h>
#include <LibWeb/CSS/StyleValues/ResolutionStyleValue.h>
#include <LibWeb/CSS/StyleValues/RevertStyleValue.h>
#include <LibWeb/CSS/StyleValues/ShadowStyleValue.h>
#include <LibWeb/CSS/StyleValues/StringStyleValue.h>
#include <LibWeb/CSS/StyleValues/StyleValueList.h>
#include <LibWeb/CSS/StyleValues/TextDecorationStyleValue.h>
#include <LibWeb/CSS/StyleValues/TimeStyleValue.h>
#include <LibWeb/CSS/StyleValues/TransformationStyleValue.h>
#include <LibWeb/CSS/StyleValues/URLStyleValue.h>
#include <LibWeb/CSS/StyleValues/UnresolvedStyleValue.h>
#include <LibWeb/CSS/StyleValues/UnsetStyleValue.h>
#include <LibWeb/Dump.h>
#include <LibWeb/Infra/Strings.h>

static void log_parse_error(SourceLocation const& location = SourceLocation::current())
{
    dbgln_if(CSS_PARSER_DEBUG, "Parse error (CSS) {}", location);
}

namespace Web::CSS::Parser {

ErrorOr<Parser> Parser::create(ParsingContext const& context, StringView input, StringView encoding)
{
    auto tokens = TRY(Tokenizer::tokenize(input, encoding));
    return Parser { context, move(tokens) };
}

Parser::Parser(ParsingContext const& context, Vector<Token> tokens)
    : m_context(context)
    , m_tokens(move(tokens))
    , m_token_stream(m_tokens)
{
}

Parser::Parser(Parser&& other)
    : m_context(other.m_context)
    , m_tokens(move(other.m_tokens))
    , m_token_stream(m_tokens)
{
    // Moving the TokenStream directly from `other` would break it, because TokenStream holds
    // a reference to the Vector<Token>, so it would be pointing at the old Parser's tokens.
    // So instead, we create a new TokenStream from this Parser's tokens, and then tell it to
    // copy the other TokenStream's state. This is quite hacky.
    m_token_stream.copy_state({}, other.m_token_stream);
}

// 5.3.3. Parse a stylesheet
// https://www.w3.org/TR/css-syntax-3/#parse-stylesheet
template<typename T>
Parser::ParsedStyleSheet Parser::parse_a_stylesheet(TokenStream<T>& tokens, Optional<AK::URL> location)
{
    // To parse a stylesheet from an input given an optional url location:

    // 1. If input is a byte stream for stylesheet, decode bytes from input, and set input to the result.
    // 2. Normalize input, and set input to the result.
    // NOTE: These are done automatically when creating the Parser.

    // 3. Create a new stylesheet, with its location set to location (or null, if location was not passed).
    ParsedStyleSheet style_sheet;
    style_sheet.location = move(location);

    // 4. Consume a list of rules from input, with the top-level flag set, and set the stylesheet’s value to the result.
    style_sheet.rules = consume_a_list_of_rules(tokens, TopLevel::Yes);

    // 5. Return the stylesheet.
    return style_sheet;
}

// https://www.w3.org/TR/css-syntax-3/#parse-a-css-stylesheet
CSSStyleSheet* Parser::parse_as_css_stylesheet(Optional<AK::URL> location)
{
    // To parse a CSS stylesheet, first parse a stylesheet.
    auto style_sheet = parse_a_stylesheet(m_token_stream, {});

    // Interpret all of the resulting top-level qualified rules as style rules, defined below.
    JS::MarkedVector<CSSRule*> rules(m_context.realm().heap());
    for (auto& raw_rule : style_sheet.rules) {
        auto* rule = convert_to_rule(raw_rule);
        // If any style rule is invalid, or any at-rule is not recognized or is invalid according to its grammar or context, it’s a parse error. Discard that rule.
        if (rule)
            rules.append(rule);
    }

    auto rule_list = CSSRuleList::create(m_context.realm(), rules);
    auto media_list = MediaList::create(m_context.realm(), {});
    return CSSStyleSheet::create(m_context.realm(), rule_list, media_list, move(location));
}

RefPtr<Supports> Parser::parse_as_supports()
{
    return parse_a_supports(m_token_stream);
}

template<typename T>
RefPtr<Supports> Parser::parse_a_supports(TokenStream<T>& tokens)
{
    auto component_values = parse_a_list_of_component_values(tokens);
    TokenStream<ComponentValue> token_stream { component_values };
    auto maybe_condition = parse_supports_condition(token_stream);
    token_stream.skip_whitespace();
    if (maybe_condition && !token_stream.has_next_token())
        return Supports::create(maybe_condition.release_nonnull());

    return {};
}

OwnPtr<Supports::Condition> Parser::parse_supports_condition(TokenStream<ComponentValue>& tokens)
{
    auto transaction = tokens.begin_transaction();
    tokens.skip_whitespace();

    auto const& peeked_token = tokens.peek_token();
    // `not <supports-in-parens>`
    if (peeked_token.is(Token::Type::Ident) && peeked_token.token().ident().equals_ignoring_ascii_case("not"sv)) {
        tokens.next_token();
        tokens.skip_whitespace();
        auto child = parse_supports_in_parens(tokens);
        if (!child.has_value())
            return {};

        transaction.commit();
        auto condition = make<Supports::Condition>();
        condition->type = Supports::Condition::Type::Not;
        condition->children.append(child.release_value());
        return condition;
    }

    // `  <supports-in-parens> [ and <supports-in-parens> ]*
    //  | <supports-in-parens> [ or <supports-in-parens> ]*`
    Vector<Supports::InParens> children;
    Optional<Supports::Condition::Type> condition_type {};
    auto as_condition_type = [](auto& token) -> Optional<Supports::Condition::Type> {
        if (!token.is(Token::Type::Ident))
            return {};
        auto ident = token.token().ident();
        if (ident.equals_ignoring_ascii_case("and"sv))
            return Supports::Condition::Type::And;
        if (ident.equals_ignoring_ascii_case("or"sv))
            return Supports::Condition::Type::Or;
        return {};
    };

    while (tokens.has_next_token()) {
        if (!children.is_empty()) {
            // Expect `and` or `or` here
            auto maybe_combination = as_condition_type(tokens.next_token());
            if (!maybe_combination.has_value())
                return {};
            if (!condition_type.has_value()) {
                condition_type = maybe_combination.value();
            } else if (maybe_combination != condition_type) {
                return {};
            }
        }

        tokens.skip_whitespace();

        if (auto in_parens = parse_supports_in_parens(tokens); in_parens.has_value()) {
            children.append(in_parens.release_value());
        } else {
            return {};
        }

        tokens.skip_whitespace();
    }

    if (children.is_empty())
        return {};

    transaction.commit();
    auto condition = make<Supports::Condition>();
    condition->type = condition_type.value_or(Supports::Condition::Type::Or);
    condition->children = move(children);
    return condition;
}

Optional<Supports::InParens> Parser::parse_supports_in_parens(TokenStream<ComponentValue>& tokens)
{
    // `( <supports-condition> )`
    auto const& first_token = tokens.peek_token();
    if (first_token.is_block() && first_token.block().is_paren()) {
        auto transaction = tokens.begin_transaction();
        tokens.next_token();
        tokens.skip_whitespace();

        TokenStream child_tokens { first_token.block().values() };
        if (auto condition = parse_supports_condition(child_tokens)) {
            if (child_tokens.has_next_token())
                return {};
            transaction.commit();
            return Supports::InParens {
                .value = { condition.release_nonnull() }
            };
        }
    }

    // `<supports-feature>`
    if (auto feature = parse_supports_feature(tokens); feature.has_value()) {
        return Supports::InParens {
            .value = { feature.release_value() }
        };
    }

    // `<general-enclosed>`
    if (auto general_enclosed = parse_general_enclosed(tokens); general_enclosed.has_value()) {
        return Supports::InParens {
            .value = general_enclosed.release_value()
        };
    }

    return {};
}

Optional<Supports::Feature> Parser::parse_supports_feature(TokenStream<ComponentValue>& tokens)
{
    auto transaction = tokens.begin_transaction();
    tokens.skip_whitespace();
    auto const& first_token = tokens.next_token();

    // `<supports-decl>`
    if (first_token.is_block() && first_token.block().is_paren()) {
        TokenStream block_tokens { first_token.block().values() };
        // FIXME: Parsing and then converting back to a string is weird.
        if (auto declaration = consume_a_declaration(block_tokens); declaration.has_value()) {
            transaction.commit();
            return Supports::Feature {
                Supports::Declaration { declaration->to_string(), JS::make_handle(m_context.realm()) }
            };
        }
    }

    // `<supports-selector-fn>`
    if (first_token.is_function() && first_token.function().name().equals_ignoring_ascii_case("selector"sv)) {
        // FIXME: Parsing and then converting back to a string is weird.
        StringBuilder builder;
        for (auto const& item : first_token.function().values())
            builder.append(item.to_string());
        transaction.commit();
        return Supports::Feature {
            Supports::Selector { builder.to_string().release_value_but_fixme_should_propagate_errors(), JS::make_handle(m_context.realm()) }
        };
    }

    return {};
}

// https://www.w3.org/TR/mediaqueries-4/#typedef-general-enclosed
Optional<GeneralEnclosed> Parser::parse_general_enclosed(TokenStream<ComponentValue>& tokens)
{
    auto transaction = tokens.begin_transaction();
    tokens.skip_whitespace();
    auto const& first_token = tokens.next_token();

    // `[ <function-token> <any-value>? ) ]`
    if (first_token.is_function()) {
        transaction.commit();
        return GeneralEnclosed { first_token.to_string() };
    }

    // `( <any-value>? )`
    if (first_token.is_block() && first_token.block().is_paren()) {
        transaction.commit();
        return GeneralEnclosed { first_token.to_string() };
    }

    return {};
}

// 5.4.1. Consume a list of rules
// https://www.w3.org/TR/css-syntax-3/#consume-list-of-rules
template<typename T>
Vector<NonnullRefPtr<Rule>> Parser::consume_a_list_of_rules(TokenStream<T>& tokens, TopLevel top_level)
{
    // To consume a list of rules, given a top-level flag:

    // Create an initially empty list of rules.
    Vector<NonnullRefPtr<Rule>> rules;

    // Repeatedly consume the next input token:
    for (;;) {
        auto& token = tokens.next_token();

        // <whitespace-token>
        if (token.is(Token::Type::Whitespace)) {
            // Do nothing.
            continue;
        }

        // <EOF-token>
        if (token.is(Token::Type::EndOfFile)) {
            // Return the list of rules.
            return rules;
        }

        // <CDO-token>
        // <CDC-token>
        if (token.is(Token::Type::CDO) || token.is(Token::Type::CDC)) {
            // If the top-level flag is set, do nothing.
            if (top_level == TopLevel::Yes)
                continue;

            // Otherwise, reconsume the current input token.
            tokens.reconsume_current_input_token();

            // Consume a qualified rule. If anything is returned, append it to the list of rules.
            if (auto maybe_qualified = consume_a_qualified_rule(tokens))
                rules.append(maybe_qualified.release_nonnull());

            continue;
        }

        // <at-keyword-token>
        if (token.is(Token::Type::AtKeyword)) {
            // Reconsume the current input token.
            tokens.reconsume_current_input_token();

            // Consume an at-rule, and append the returned value to the list of rules.
            rules.append(consume_an_at_rule(tokens));

            continue;
        }

        // anything else
        {
            // Reconsume the current input token.
            tokens.reconsume_current_input_token();

            // Consume a qualified rule. If anything is returned, append it to the list of rules.
            if (auto maybe_qualified = consume_a_qualified_rule(tokens))
                rules.append(maybe_qualified.release_nonnull());

            continue;
        }
    }
}

// 5.4.2. Consume an at-rule
// https://www.w3.org/TR/css-syntax-3/#consume-at-rule
template<typename T>
NonnullRefPtr<Rule> Parser::consume_an_at_rule(TokenStream<T>& tokens)
{
    // To consume an at-rule:

    // Consume the next input token.
    auto& name_ident = tokens.next_token();
    VERIFY(name_ident.is(Token::Type::AtKeyword));

    // Create a new at-rule with its name set to the value of the current input token, its prelude initially set to an empty list, and its value initially set to nothing.
    // NOTE: We create the Rule fully initialized when we return it instead.
    auto at_rule_name = FlyString::from_utf8(((Token)name_ident).at_keyword()).release_value_but_fixme_should_propagate_errors();
    Vector<ComponentValue> prelude;
    RefPtr<Block> block;

    // Repeatedly consume the next input token:
    for (;;) {
        auto& token = tokens.next_token();

        // <semicolon-token>
        if (token.is(Token::Type::Semicolon)) {
            // Return the at-rule.
            return Rule::make_at_rule(move(at_rule_name), move(prelude), move(block));
        }

        // <EOF-token>
        if (token.is(Token::Type::EndOfFile)) {
            // This is a parse error. Return the at-rule.
            log_parse_error();
            return Rule::make_at_rule(move(at_rule_name), move(prelude), move(block));
        }

        // <{-token>
        if (token.is(Token::Type::OpenCurly)) {
            // Consume a simple block and assign it to the at-rule’s block. Return the at-rule.
            block = consume_a_simple_block(tokens);
            return Rule::make_at_rule(move(at_rule_name), move(prelude), move(block));
        }

        // simple block with an associated token of <{-token>
        if constexpr (IsSame<T, ComponentValue>) {
            ComponentValue const& component_value = token;
            if (component_value.is_block() && component_value.block().is_curly()) {
                // Assign the block to the at-rule’s block. Return the at-rule.
                block = component_value.block();
                return Rule::make_at_rule(move(at_rule_name), move(prelude), move(block));
            }
        }

        // anything else
        {
            // Reconsume the current input token.
            tokens.reconsume_current_input_token();
            // Consume a component value. Append the returned value to the at-rule’s prelude.
            prelude.append(consume_a_component_value(tokens));
        }
    }
}

// 5.4.3. Consume a qualified rule
// https://www.w3.org/TR/css-syntax-3/#consume-qualified-rule
template<typename T>
RefPtr<Rule> Parser::consume_a_qualified_rule(TokenStream<T>& tokens)
{
    // To consume a qualified rule:

    // Create a new qualified rule with its prelude initially set to an empty list, and its value initially set to nothing.
    // NOTE: We create the Rule fully initialized when we return it instead.
    Vector<ComponentValue> prelude;
    RefPtr<Block> block;

    // Repeatedly consume the next input token:
    for (;;) {
        auto& token = tokens.next_token();

        // <EOF-token>
        if (token.is(Token::Type::EndOfFile)) {
            // This is a parse error. Return nothing.
            log_parse_error();
            return {};
        }

        // <{-token>
        if (token.is(Token::Type::OpenCurly)) {
            // Consume a simple block and assign it to the qualified rule’s block. Return the qualified rule.
            block = consume_a_simple_block(tokens);
            return Rule::make_qualified_rule(move(prelude), move(block));
        }

        // simple block with an associated token of <{-token>
        if constexpr (IsSame<T, ComponentValue>) {
            ComponentValue const& component_value = token;
            if (component_value.is_block() && component_value.block().is_curly()) {
                // Assign the block to the qualified rule’s block. Return the qualified rule.
                block = component_value.block();
                return Rule::make_qualified_rule(move(prelude), move(block));
            }
        }

        // anything else
        {
            // Reconsume the current input token.
            tokens.reconsume_current_input_token();

            // Consume a component value. Append the returned value to the qualified rule’s prelude.
            prelude.append(consume_a_component_value(tokens));
        }
    }
}

// 5.4.4. Consume a style block’s contents
// https://www.w3.org/TR/css-syntax-3/#consume-a-style-blocks-contents
template<typename T>
Vector<DeclarationOrAtRule> Parser::consume_a_style_blocks_contents(TokenStream<T>& tokens)
{
    // To consume a style block’s contents:
    // Create an initially empty list of declarations decls, and an initially empty list of rules rules.
    Vector<DeclarationOrAtRule> declarations;
    Vector<DeclarationOrAtRule> rules;

    // Repeatedly consume the next input token:
    for (;;) {
        auto& token = tokens.next_token();

        // <whitespace-token>
        // <semicolon-token>
        if (token.is(Token::Type::Whitespace) || token.is(Token::Type::Semicolon)) {
            // Do nothing.
            continue;
        }

        // <EOF-token>
        if (token.is(Token::Type::EndOfFile)) {
            // Extend decls with rules, then return decls.
            declarations.extend(move(rules));
            return declarations;
        }

        // <at-keyword-token>
        if (token.is(Token::Type::AtKeyword)) {
            // Reconsume the current input token.
            tokens.reconsume_current_input_token();

            // Consume an at-rule, and append the result to rules.
            rules.empend(consume_an_at_rule(tokens));
            continue;
        }

        // <ident-token>
        if (token.is(Token::Type::Ident)) {
            // Initialize a temporary list initially filled with the current input token.
            Vector<ComponentValue> temporary_list;
            temporary_list.append(token);

            // As long as the next input token is anything other than a <semicolon-token> or <EOF-token>,
            // consume a component value and append it to the temporary list.
            for (;;) {
                auto& next_input_token = tokens.peek_token();
                if (next_input_token.is(Token::Type::Semicolon) || next_input_token.is(Token::Type::EndOfFile))
                    break;
                temporary_list.append(consume_a_component_value(tokens));
            }

            // Consume a declaration from the temporary list. If anything was returned, append it to decls.
            auto token_stream = TokenStream(temporary_list);
            if (auto maybe_declaration = consume_a_declaration(token_stream); maybe_declaration.has_value())
                declarations.empend(maybe_declaration.release_value());

            continue;
        }

        // <delim-token> with a value of "&" (U+0026 AMPERSAND)
        if (token.is_delim('&')) {
            // Reconsume the current input token.
            tokens.reconsume_current_input_token();

            // Consume a qualified rule. If anything was returned, append it to rules.
            if (auto qualified_rule = consume_a_qualified_rule(tokens))
                rules.empend(qualified_rule);

            continue;
        }

        // anything else
        {
            // This is a parse error.
            log_parse_error();

            // Reconsume the current input token.
            tokens.reconsume_current_input_token();

            // As long as the next input token is anything other than a <semicolon-token> or <EOF-token>,
            // consume a component value and throw away the returned value.
            for (;;) {
                auto& peek = tokens.peek_token();
                if (peek.is(Token::Type::Semicolon) || peek.is(Token::Type::EndOfFile))
                    break;
                (void)consume_a_component_value(tokens);
            }
        }
    }
}

template<>
ComponentValue Parser::consume_a_component_value(TokenStream<ComponentValue>& tokens)
{
    // Note: This overload is called once tokens have already been converted into component values,
    //       so we do not need to do the work in the more general overload.
    return tokens.next_token();
}

// 5.4.7. Consume a component value
// https://www.w3.org/TR/css-syntax-3/#consume-component-value
template<typename T>
ComponentValue Parser::consume_a_component_value(TokenStream<T>& tokens)
{
    // To consume a component value:

    // Consume the next input token.
    auto& token = tokens.next_token();

    // If the current input token is a <{-token>, <[-token>, or <(-token>, consume a simple block and return it.
    if (token.is(Token::Type::OpenCurly) || token.is(Token::Type::OpenSquare) || token.is(Token::Type::OpenParen))
        return ComponentValue(consume_a_simple_block(tokens));

    // Otherwise, if the current input token is a <function-token>, consume a function and return it.
    if (token.is(Token::Type::Function))
        return ComponentValue(consume_a_function(tokens));

    // Otherwise, return the current input token.
    return ComponentValue(token);
}

// 5.4.8. Consume a simple block
// https://www.w3.org/TR/css-syntax-3/#consume-simple-block
template<typename T>
NonnullRefPtr<Block> Parser::consume_a_simple_block(TokenStream<T>& tokens)
{
    // Note: This algorithm assumes that the current input token has already been checked
    // to be an <{-token>, <[-token>, or <(-token>.

    // To consume a simple block:

    // The ending token is the mirror variant of the current input token.
    // (E.g. if it was called with <[-token>, the ending token is <]-token>.)
    auto ending_token = ((Token)tokens.current_token()).mirror_variant();

    // Create a simple block with its associated token set to the current input token
    // and with its value initially set to an empty list.
    // NOTE: We create the Block fully initialized when we return it instead.
    Token block_token = tokens.current_token();
    Vector<ComponentValue> block_values;

    // Repeatedly consume the next input token and process it as follows:
    for (;;) {
        auto& token = tokens.next_token();

        // ending token
        if (token.is(ending_token)) {
            // Return the block.
            return Block::create(move(block_token), move(block_values));
        }
        // <EOF-token>
        if (token.is(Token::Type::EndOfFile)) {
            // This is a parse error. Return the block.
            log_parse_error();
            return Block::create(move(block_token), move(block_values));
        }

        // anything else
        {
            // Reconsume the current input token.
            tokens.reconsume_current_input_token();

            // Consume a component value and append it to the value of the block.
            block_values.empend(consume_a_component_value(tokens));
        }
    }
}

// 5.4.9. Consume a function
// https://www.w3.org/TR/css-syntax-3/#consume-function
template<typename T>
NonnullRefPtr<Function> Parser::consume_a_function(TokenStream<T>& tokens)
{
    // Note: This algorithm assumes that the current input token has already been checked to be a <function-token>.
    auto name_ident = tokens.current_token();
    VERIFY(name_ident.is(Token::Type::Function));

    // To consume a function:

    // Create a function with its name equal to the value of the current input token
    // and with its value initially set to an empty list.
    // NOTE: We create the Function fully initialized when we return it instead.
    auto function_name = FlyString::from_utf8(((Token)name_ident).function()).release_value_but_fixme_should_propagate_errors();
    Vector<ComponentValue> function_values;

    // Repeatedly consume the next input token and process it as follows:
    for (;;) {
        auto& token = tokens.next_token();

        // <)-token>
        if (token.is(Token::Type::CloseParen)) {
            // Return the function.
            return Function::create(move(function_name), move(function_values));
        }

        // <EOF-token>
        if (token.is(Token::Type::EndOfFile)) {
            // This is a parse error. Return the function.
            log_parse_error();
            return Function::create(move(function_name), move(function_values));
        }

        // anything else
        {
            // Reconsume the current input token.
            tokens.reconsume_current_input_token();

            // Consume a component value and append the returned value to the function’s value.
            function_values.append(consume_a_component_value(tokens));
        }
    }
}

// 5.4.6. Consume a declaration
// https://www.w3.org/TR/css-syntax-3/#consume-declaration
template<typename T>
Optional<Declaration> Parser::consume_a_declaration(TokenStream<T>& tokens)
{
    // Note: This algorithm assumes that the next input token has already been checked to
    // be an <ident-token>.
    // NOTE: This is not true in our implementation! For convenience, we both skip whitespace
    //       and gracefully handle the first token not being an <ident-token>.

    // To consume a declaration:

    // Consume the next input token.
    auto transaction = tokens.begin_transaction();
    tokens.skip_whitespace();
    auto& token = tokens.next_token();

    // NOTE: Not to spec, handle the case where the input token *isn't* an <ident-token>.
    if (!token.is(Token::Type::Ident))
        return {};

    // Create a new declaration with its name set to the value of the current input token
    // and its value initially set to the empty list.
    // NOTE: We create a fully-initialized Declaration just before returning it instead.
    auto declaration_name = FlyString::from_utf8(((Token)token).ident()).release_value_but_fixme_should_propagate_errors();
    Vector<ComponentValue> declaration_values;
    Important declaration_important = Important::No;

    // 1. While the next input token is a <whitespace-token>, consume the next input token.
    tokens.skip_whitespace();

    // 2. If the next input token is anything other than a <colon-token>, this is a parse error.
    // Return nothing.
    auto& maybe_colon = tokens.peek_token();
    if (!maybe_colon.is(Token::Type::Colon)) {
        log_parse_error();
        return {};
    }
    // Otherwise, consume the next input token.
    tokens.next_token();

    // 3. While the next input token is a <whitespace-token>, consume the next input token.
    tokens.skip_whitespace();

    // 4. As long as the next input token is anything other than an <EOF-token>, consume a
    //    component value and append it to the declaration’s value.
    for (;;) {
        if (tokens.peek_token().is(Token::Type::EndOfFile)) {
            break;
        }
        declaration_values.append(consume_a_component_value(tokens));
    }

    // 5. If the last two non-<whitespace-token>s in the declaration’s value are a <delim-token>
    //    with the value "!" followed by an <ident-token> with a value that is an ASCII case-insensitive
    //    match for "important", remove them from the declaration’s value and set the declaration’s
    //    important flag to true.
    if (declaration_values.size() >= 2) {
        // Walk backwards from the end until we find "important"
        Optional<size_t> important_index;
        for (size_t i = declaration_values.size() - 1; i > 0; i--) {
            auto value = declaration_values[i];
            if (value.is(Token::Type::Ident) && Infra::is_ascii_case_insensitive_match(value.token().ident(), "important"sv)) {
                important_index = i;
                break;
            }
            if (value.is(Token::Type::Whitespace))
                continue;
            break;
        }

        // Walk backwards from important until we find "!"
        if (important_index.has_value()) {
            Optional<size_t> bang_index;
            for (size_t i = important_index.value() - 1; i > 0; i--) {
                auto value = declaration_values[i];
                if (value.is_delim('!')) {
                    bang_index = i;
                    break;
                }
                if (value.is(Token::Type::Whitespace))
                    continue;
                break;
            }

            if (bang_index.has_value()) {
                declaration_values.remove(important_index.value());
                declaration_values.remove(bang_index.value());
                declaration_important = Important::Yes;
            }
        }
    }

    // 6. While the last token in the declaration’s value is a <whitespace-token>, remove that token.
    while (!declaration_values.is_empty()) {
        auto maybe_whitespace = declaration_values.last();
        if (!(maybe_whitespace.is(Token::Type::Whitespace))) {
            break;
        }
        declaration_values.take_last();
    }

    // 7. Return the declaration.
    transaction.commit();
    return Declaration { move(declaration_name), move(declaration_values), declaration_important };
}

// 5.4.5. Consume a list of declarations
// https://www.w3.org/TR/css-syntax-3/#consume-list-of-declarations
template<typename T>
Vector<DeclarationOrAtRule> Parser::consume_a_list_of_declarations(TokenStream<T>& tokens)
{
    // To consume a list of declarations:

    // Create an initially empty list of declarations.
    Vector<DeclarationOrAtRule> list_of_declarations;

    // Repeatedly consume the next input token:
    for (;;) {
        auto& token = tokens.next_token();

        // <whitespace-token>
        // <semicolon-token>
        if (token.is(Token::Type::Whitespace) || token.is(Token::Type::Semicolon)) {
            // Do nothing.
            continue;
        }

        // <EOF-token>
        if (token.is(Token::Type::EndOfFile)) {
            // Return the list of declarations.
            return list_of_declarations;
        }

        // <at-keyword-token>
        if (token.is(Token::Type::AtKeyword)) {
            // Reconsume the current input token.
            tokens.reconsume_current_input_token();

            // Consume an at-rule. Append the returned rule to the list of declarations.
            list_of_declarations.empend(consume_an_at_rule(tokens));
            continue;
        }

        // <ident-token>
        if (token.is(Token::Type::Ident)) {
            // Initialize a temporary list initially filled with the current input token.
            Vector<ComponentValue> temporary_list;
            temporary_list.append(token);

            // As long as the next input token is anything other than a <semicolon-token> or <EOF-token>,
            // consume a component value and append it to the temporary list.
            for (;;) {
                auto& peek = tokens.peek_token();
                if (peek.is(Token::Type::Semicolon) || peek.is(Token::Type::EndOfFile))
                    break;
                temporary_list.append(consume_a_component_value(tokens));
            }

            // Consume a declaration from the temporary list. If anything was returned, append it to the list of declarations.
            auto token_stream = TokenStream(temporary_list);
            if (auto maybe_declaration = consume_a_declaration(token_stream); maybe_declaration.has_value())
                list_of_declarations.empend(maybe_declaration.value());

            continue;
        }

        // anything else
        {
            // This is a parse error.
            log_parse_error();

            // Reconsume the current input token.
            tokens.reconsume_current_input_token();

            // As long as the next input token is anything other than a <semicolon-token> or <EOF-token>,
            // consume a component value and throw away the returned value.
            for (;;) {
                auto& peek = tokens.peek_token();
                if (peek.is(Token::Type::Semicolon) || peek.is(Token::Type::EndOfFile))
                    break;
                dbgln_if(CSS_PARSER_DEBUG, "Discarding token: '{}'", peek.to_debug_string());
                (void)consume_a_component_value(tokens);
            }
        }
    }
}

CSSRule* Parser::parse_as_css_rule()
{
    auto maybe_rule = parse_a_rule(m_token_stream);
    if (maybe_rule)
        return convert_to_rule(maybe_rule.release_nonnull());
    return {};
}

// 5.3.5. Parse a rule
// https://www.w3.org/TR/css-syntax-3/#parse-rule
template<typename T>
RefPtr<Rule> Parser::parse_a_rule(TokenStream<T>& tokens)
{
    // To parse a rule from input:
    RefPtr<Rule> rule;

    // 1. Normalize input, and set input to the result.
    // Note: This is done when initializing the Parser.

    // 2. While the next input token from input is a <whitespace-token>, consume the next input token from input.
    tokens.skip_whitespace();

    // 3. If the next input token from input is an <EOF-token>, return a syntax error.
    auto& token = tokens.peek_token();
    if (token.is(Token::Type::EndOfFile)) {
        return {};
    }
    // Otherwise, if the next input token from input is an <at-keyword-token>, consume an at-rule from input, and let rule be the return value.
    else if (token.is(Token::Type::AtKeyword)) {
        rule = consume_an_at_rule(m_token_stream);
    }
    // Otherwise, consume a qualified rule from input and let rule be the return value. If nothing was returned, return a syntax error.
    else {
        auto qualified_rule = consume_a_qualified_rule(tokens);
        if (!qualified_rule)
            return {};

        rule = qualified_rule;
    }

    // 4. While the next input token from input is a <whitespace-token>, consume the next input token from input.
    tokens.skip_whitespace();

    // 5. If the next input token from input is an <EOF-token>, return rule. Otherwise, return a syntax error.
    if (tokens.peek_token().is(Token::Type::EndOfFile))
        return rule;
    return {};
}

// 5.3.4. Parse a list of rules
// https://www.w3.org/TR/css-syntax-3/#parse-list-of-rules
template<typename T>
Vector<NonnullRefPtr<Rule>> Parser::parse_a_list_of_rules(TokenStream<T>& tokens)
{
    // To parse a list of rules from input:

    // 1. Normalize input, and set input to the result.
    // Note: This is done when initializing the Parser.

    // 2. Consume a list of rules from the input, with the top-level flag unset.
    auto list_of_rules = consume_a_list_of_rules(tokens, TopLevel::No);

    // 3. Return the returned list.
    return list_of_rules;
}
template Vector<NonnullRefPtr<Rule>> Parser::parse_a_list_of_rules(TokenStream<Token>& tokens);
template Vector<NonnullRefPtr<Rule>> Parser::parse_a_list_of_rules(TokenStream<ComponentValue>& tokens);

Optional<StyleProperty> Parser::parse_as_supports_condition()
{
    auto maybe_declaration = parse_a_declaration(m_token_stream);
    if (maybe_declaration.has_value())
        return convert_to_style_property(maybe_declaration.release_value());
    return {};
}

// 5.3.6. Parse a declaration
// https://www.w3.org/TR/css-syntax-3/#parse-a-declaration
template<typename T>
Optional<Declaration> Parser::parse_a_declaration(TokenStream<T>& tokens)
{
    // To parse a declaration from input:

    // 1. Normalize input, and set input to the result.
    // Note: This is done when initializing the Parser.

    // 2. While the next input token from input is a <whitespace-token>, consume the next input token.
    tokens.skip_whitespace();

    // 3. If the next input token from input is not an <ident-token>, return a syntax error.
    auto& token = tokens.peek_token();
    if (!token.is(Token::Type::Ident)) {
        return {};
    }

    // 4. Consume a declaration from input. If anything was returned, return it. Otherwise, return a syntax error.
    if (auto declaration = consume_a_declaration(tokens); declaration.has_value())
        return declaration.release_value();
    return {};
}

// 5.3.7. Parse a style block’s contents
// https://www.w3.org/TR/css-syntax-3/#parse-style-blocks-contents
template<typename T>
Vector<DeclarationOrAtRule> Parser::parse_a_style_blocks_contents(TokenStream<T>& tokens)
{
    // To parse a style block’s contents from input:

    // 1. Normalize input, and set input to the result.
    // Note: This is done when initializing the Parser.

    // 2. Consume a style block’s contents from input, and return the result.
    return consume_a_style_blocks_contents(tokens);
}

// 5.3.8. Parse a list of declarations
// https://www.w3.org/TR/css-syntax-3/#parse-list-of-declarations
template<typename T>
Vector<DeclarationOrAtRule> Parser::parse_a_list_of_declarations(TokenStream<T>& tokens)
{
    // To parse a list of declarations from input:

    // 1. Normalize input, and set input to the result.
    // Note: This is done when initializing the Parser.

    // 2. Consume a list of declarations from input, and return the result.
    return consume_a_list_of_declarations(tokens);
}

// 5.3.9. Parse a component value
// https://www.w3.org/TR/css-syntax-3/#parse-component-value
template<typename T>
Optional<ComponentValue> Parser::parse_a_component_value(TokenStream<T>& tokens)
{
    // To parse a component value from input:

    // 1. Normalize input, and set input to the result.
    // Note: This is done when initializing the Parser.

    // 2. While the next input token from input is a <whitespace-token>, consume the next input token from input.
    tokens.skip_whitespace();

    // 3. If the next input token from input is an <EOF-token>, return a syntax error.
    if (tokens.peek_token().is(Token::Type::EndOfFile))
        return {};

    // 4. Consume a component value from input and let value be the return value.
    auto value = consume_a_component_value(tokens);

    // 5. While the next input token from input is a <whitespace-token>, consume the next input token.
    tokens.skip_whitespace();

    // 6. If the next input token from input is an <EOF-token>, return value. Otherwise, return a syntax error.
    if (tokens.peek_token().is(Token::Type::EndOfFile))
        return value;
    return {};
}

// 5.3.10. Parse a list of component values
// https://www.w3.org/TR/css-syntax-3/#parse-list-of-component-values
template<typename T>
Vector<ComponentValue> Parser::parse_a_list_of_component_values(TokenStream<T>& tokens)
{
    // To parse a list of component values from input:

    // 1. Normalize input, and set input to the result.
    // Note: This is done when initializing the Parser.

    // 2. Repeatedly consume a component value from input until an <EOF-token> is returned, appending the returned values (except the final <EOF-token>) into a list. Return the list.
    Vector<ComponentValue> component_values;

    for (;;) {
        if (tokens.peek_token().is(Token::Type::EndOfFile)) {
            break;
        }

        component_values.append(consume_a_component_value(tokens));
    }

    return component_values;
}

// 5.3.11. Parse a comma-separated list of component values
// https://www.w3.org/TR/css-syntax-3/#parse-comma-separated-list-of-component-values
template<typename T>
Vector<Vector<ComponentValue>> Parser::parse_a_comma_separated_list_of_component_values(TokenStream<T>& tokens)
{
    // To parse a comma-separated list of component values from input:

    // 1. Normalize input, and set input to the result.
    // Note: This is done when initializing the Parser.

    // 2. Let list of cvls be an initially empty list of component value lists.
    Vector<Vector<ComponentValue>> list_of_component_value_lists;

    // 3. Repeatedly consume a component value from input until an <EOF-token> or <comma-token> is returned,
    //    appending the returned values (except the final <EOF-token> or <comma-token>) into a list.
    //    Append the list to list of cvls.
    //    If it was a <comma-token> that was returned, repeat this step.
    Vector<ComponentValue> current_list;
    for (;;) {
        auto component_value = consume_a_component_value(tokens);

        if (component_value.is(Token::Type::EndOfFile)) {
            list_of_component_value_lists.append(move(current_list));
            break;
        }
        if (component_value.is(Token::Type::Comma)) {
            list_of_component_value_lists.append(move(current_list));
            current_list = {};
            continue;
        }

        current_list.append(component_value);
    }

    // 4. Return list of cvls.
    return list_of_component_value_lists;
}
template Vector<Vector<ComponentValue>> Parser::parse_a_comma_separated_list_of_component_values(TokenStream<ComponentValue>&);
template Vector<Vector<ComponentValue>> Parser::parse_a_comma_separated_list_of_component_values(TokenStream<Token>&);

ElementInlineCSSStyleDeclaration* Parser::parse_as_style_attribute(DOM::Element& element)
{
    auto declarations_and_at_rules = parse_a_list_of_declarations(m_token_stream);
    auto [properties, custom_properties] = extract_properties(declarations_and_at_rules);
    return ElementInlineCSSStyleDeclaration::create(element, move(properties), move(custom_properties));
}

Optional<AK::URL> Parser::parse_url_function(ComponentValue const& component_value)
{
    // FIXME: Handle list of media queries. https://www.w3.org/TR/css-cascade-3/#conditional-import

    auto convert_string_to_url = [&](StringView& url_string) -> Optional<AK::URL> {
        auto url = m_context.complete_url(url_string);
        if (url.is_valid())
            return url;
        return {};
    };

    if (component_value.is(Token::Type::Url)) {
        auto url_string = component_value.token().url();
        return convert_string_to_url(url_string);
    }
    if (component_value.is_function() && component_value.function().name().equals_ignoring_ascii_case("url"sv)) {
        auto const& function_values = component_value.function().values();
        // FIXME: Handle url-modifiers. https://www.w3.org/TR/css-values-4/#url-modifiers
        for (size_t i = 0; i < function_values.size(); ++i) {
            auto const& value = function_values[i];
            if (value.is(Token::Type::Whitespace))
                continue;
            if (value.is(Token::Type::String)) {
                auto url_string = value.token().string();
                return convert_string_to_url(url_string);
            }
            break;
        }
    }

    return {};
}

RefPtr<StyleValue> Parser::parse_url_value(ComponentValue const& component_value)
{
    auto url = parse_url_function(component_value);
    if (!url.has_value())
        return nullptr;
    return URLStyleValue::create(*url);
}

Optional<PositionValue> Parser::parse_position(TokenStream<ComponentValue>& tokens, PositionValue initial_value)
{
    auto transaction = tokens.begin_transaction();
    tokens.skip_whitespace();
    if (!tokens.has_next_token())
        return {};

    auto parse_horizontal_preset = [&](auto ident) -> Optional<PositionValue::HorizontalPreset> {
        if (ident.equals_ignoring_ascii_case("left"sv))
            return PositionValue::HorizontalPreset::Left;
        if (ident.equals_ignoring_ascii_case("center"sv))
            return PositionValue::HorizontalPreset::Center;
        if (ident.equals_ignoring_ascii_case("right"sv))
            return PositionValue::HorizontalPreset::Right;
        return {};
    };

    auto parse_vertical_preset = [&](auto ident) -> Optional<PositionValue::VerticalPreset> {
        if (ident.equals_ignoring_ascii_case("top"sv))
            return PositionValue::VerticalPreset::Top;
        if (ident.equals_ignoring_ascii_case("center"sv))
            return PositionValue::VerticalPreset::Center;
        if (ident.equals_ignoring_ascii_case("bottom"sv))
            return PositionValue::VerticalPreset::Bottom;
        return {};
    };

    auto parse_horizontal_edge = [&](auto ident) -> Optional<PositionValue::HorizontalEdge> {
        if (ident.equals_ignoring_ascii_case("left"sv))
            return PositionValue::HorizontalEdge::Left;
        if (ident.equals_ignoring_ascii_case("right"sv))
            return PositionValue::HorizontalEdge::Right;
        return {};
    };

    auto parse_vertical_edge = [&](auto ident) -> Optional<PositionValue::VerticalEdge> {
        if (ident.equals_ignoring_ascii_case("top"sv))
            return PositionValue::VerticalEdge::Top;
        if (ident.equals_ignoring_ascii_case("bottom"sv))
            return PositionValue::VerticalEdge::Bottom;
        return {};
    };

    // <position> = [
    //   [ left | center | right ] || [ top | center | bottom ]
    // |
    //   [ left | center | right | <length-percentage> ]
    //   [ top | center | bottom | <length-percentage> ]?
    // |
    //   [ [ left | right ] <length-percentage> ] &&
    //   [ [ top | bottom ] <length-percentage> ]
    // ]

    // [ left | center | right ] || [ top | center | bottom ]
    auto alternation_1 = [&]() -> Optional<PositionValue> {
        auto transaction = tokens.begin_transaction();
        PositionValue position = initial_value;
        auto& first_token = tokens.next_token();
        if (!first_token.is(Token::Type::Ident))
            return {};
        auto ident = first_token.token().ident();
        // <horizontal-position> <vertical-position>?
        auto horizontal_position = parse_horizontal_preset(ident);
        if (horizontal_position.has_value()) {
            position.horizontal_position = *horizontal_position;
            auto transaction_optional_parse = tokens.begin_transaction();
            tokens.skip_whitespace();
            if (tokens.has_next_token()) {
                auto& second_token = tokens.next_token();
                if (second_token.is(Token::Type::Ident)) {
                    auto vertical_position = parse_vertical_preset(second_token.token().ident());
                    if (vertical_position.has_value()) {
                        transaction_optional_parse.commit();
                        position.vertical_position = *vertical_position;
                    }
                }
            }
        } else {
            // <vertical-position> <horizontal-position>?
            auto vertical_position = parse_vertical_preset(ident);
            if (!vertical_position.has_value())
                return {};
            position.vertical_position = *vertical_position;
            auto transaction_optional_parse = tokens.begin_transaction();
            tokens.skip_whitespace();
            if (tokens.has_next_token()) {
                auto& second_token = tokens.next_token();
                if (second_token.is(Token::Type::Ident)) {
                    auto horizontal_position = parse_horizontal_preset(second_token.token().ident());
                    if (horizontal_position.has_value()) {
                        transaction_optional_parse.commit();
                        position.horizontal_position = *horizontal_position;
                    }
                }
            }
        }
        transaction.commit();
        return position;
    };

    // [ left | center | right | <length-percentage> ]
    // [ top | center | bottom | <length-percentage> ]?
    auto alternation_2 = [&]() -> Optional<PositionValue> {
        auto transaction = tokens.begin_transaction();
        PositionValue position = initial_value;
        auto& first_token = tokens.next_token();
        if (first_token.is(Token::Type::Ident)) {
            auto horizontal_position = parse_horizontal_preset(first_token.token().ident());
            if (!horizontal_position.has_value())
                return {};
            position.horizontal_position = *horizontal_position;
        } else {
            auto dimension = parse_dimension(first_token);
            if (!dimension.has_value() || !dimension->is_length_percentage())
                return {};
            position.horizontal_position = dimension->length_percentage();
        }
        auto transaction_optional_parse = tokens.begin_transaction();
        tokens.skip_whitespace();
        if (tokens.has_next_token()) {
            auto& second_token = tokens.next_token();
            if (second_token.is(Token::Type::Ident)) {
                auto vertical_position = parse_vertical_preset(second_token.token().ident());
                if (vertical_position.has_value()) {
                    transaction_optional_parse.commit();
                    position.vertical_position = *vertical_position;
                }
            } else {
                auto dimension = parse_dimension(second_token);
                if (dimension.has_value() && dimension->is_length_percentage()) {
                    transaction_optional_parse.commit();
                    position.vertical_position = dimension->length_percentage();
                }
            }
        }
        transaction.commit();
        return position;
    };

    // [ [ left | right ] <length-percentage> ] &&
    // [ [ top | bottom ] <length-percentage> ]
    auto alternation_3 = [&]() -> Optional<PositionValue> {
        auto transaction = tokens.begin_transaction();
        PositionValue position {};

        auto parse_horizontal = [&] {
            // [ left | right ] <length-percentage> ]
            auto transaction = tokens.begin_transaction();
            tokens.skip_whitespace();
            if (!tokens.has_next_token())
                return false;
            auto& first_token = tokens.next_token();
            if (!first_token.is(Token::Type::Ident))
                return false;
            auto horizontal_egde = parse_horizontal_edge(first_token.token().ident());
            if (!horizontal_egde.has_value())
                return false;
            position.x_relative_to = *horizontal_egde;
            tokens.skip_whitespace();
            if (!tokens.has_next_token())
                return false;
            auto& second_token = tokens.next_token();
            auto dimension = parse_dimension(second_token);
            if (!dimension.has_value() || !dimension->is_length_percentage())
                return false;
            position.horizontal_position = dimension->length_percentage();
            transaction.commit();
            return true;
        };

        auto parse_vertical = [&] {
            // [ top | bottom ] <length-percentage> ]
            auto transaction = tokens.begin_transaction();
            tokens.skip_whitespace();
            if (!tokens.has_next_token())
                return false;
            auto& first_token = tokens.next_token();
            if (!first_token.is(Token::Type::Ident))
                return false;
            auto vertical_edge = parse_vertical_edge(first_token.token().ident());
            if (!vertical_edge.has_value())
                return false;
            position.y_relative_to = *vertical_edge;
            tokens.skip_whitespace();
            if (!tokens.has_next_token())
                return false;
            auto& second_token = tokens.next_token();
            auto dimension = parse_dimension(second_token);
            if (!dimension.has_value() || !dimension->is_length_percentage())
                return false;
            position.vertical_position = dimension->length_percentage();
            transaction.commit();
            return true;
        };

        if ((parse_horizontal() && parse_vertical()) || (parse_vertical() && parse_horizontal())) {
            transaction.commit();
            return position;
        }

        return {};
    };

    // Note: The alternatives must be attempted in this order since `alternation_2' can match a prefix of `alternation_3'
    auto position = alternation_3();
    if (!position.has_value())
        position = alternation_2();
    if (!position.has_value())
        position = alternation_1();
    if (position.has_value())
        transaction.commit();
    return position;
}

CSSRule* Parser::convert_to_rule(NonnullRefPtr<Rule> rule)
{
    if (rule->is_at_rule()) {
        if (has_ignored_vendor_prefix(rule->at_rule_name()))
            return {};
        if (rule->at_rule_name().equals_ignoring_ascii_case("font-face"sv)) {
            if (!rule->block() || !rule->block()->is_curly()) {
                dbgln_if(CSS_PARSER_DEBUG, "@font-face rule is malformed.");
                return {};
            }
            TokenStream tokens { rule->block()->values() };
            return parse_font_face_rule(tokens);
        }
        if (rule->at_rule_name().equals_ignoring_ascii_case("import"sv) && !rule->prelude().is_empty()) {
            Optional<AK::URL> url;
            for (auto const& token : rule->prelude()) {
                if (token.is(Token::Type::Whitespace))
                    continue;

                if (token.is(Token::Type::String)) {
                    url = m_context.complete_url(token.token().string());
                } else {
                    url = parse_url_function(token);
                }

                // FIXME: Handle list of media queries. https://www.w3.org/TR/css-cascade-3/#conditional-import
                if (url.has_value())
                    break;
            }

            if (url.has_value())
                return CSSImportRule::create(url.value(), const_cast<DOM::Document&>(*m_context.document()));
            dbgln_if(CSS_PARSER_DEBUG, "Unable to parse url from @import rule");
            return {};
        }
        if (rule->at_rule_name().equals_ignoring_ascii_case("media"sv))
            return convert_to_media_rule(rule);
        if (rule->at_rule_name().equals_ignoring_ascii_case("supports"sv)) {
            auto supports_tokens = TokenStream { rule->prelude() };
            auto supports = parse_a_supports(supports_tokens);
            if (!supports) {
                if constexpr (CSS_PARSER_DEBUG) {
                    dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @supports rule invalid; discarding.");
                    supports_tokens.dump_all_tokens();
                }
                return {};
            }

            if (!rule->block())
                return {};
            auto child_tokens = TokenStream { rule->block()->values() };
            auto parser_rules = parse_a_list_of_rules(child_tokens);
            JS::MarkedVector<CSSRule*> child_rules(m_context.realm().heap());
            for (auto& raw_rule : parser_rules) {
                if (auto* child_rule = convert_to_rule(raw_rule))
                    child_rules.append(child_rule);
            }

            auto rule_list = CSSRuleList::create(m_context.realm(), child_rules);
            return CSSSupportsRule::create(m_context.realm(), supports.release_nonnull(), rule_list);
        }
        if (rule->at_rule_name().equals_ignoring_ascii_case("keyframes"sv)) {
            auto prelude_stream = TokenStream { rule->prelude() };
            prelude_stream.skip_whitespace();
            auto token = prelude_stream.next_token();
            if (!token.is_token()) {
                dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @keyframes has invalid prelude, prelude = {}; discarding.", rule->prelude());
                return {};
            }

            auto name_token = token.token();
            prelude_stream.skip_whitespace();

            if (prelude_stream.has_next_token()) {
                dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @keyframes has invalid prelude, prelude = {}; discarding.", rule->prelude());
                return {};
            }

            if (name_token.is(Token::Type::Ident) && (is_builtin(name_token.ident()) || name_token.ident().equals_ignoring_ascii_case("none"sv))) {
                dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @keyframes rule name is invalid: {}; discarding.", name_token.ident());
                return {};
            }

            if (!name_token.is(Token::Type::String) && !name_token.is(Token::Type::Ident)) {
                dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @keyframes rule name is invalid: {}; discarding.", name_token.to_debug_string());
                return {};
            }

            auto name = name_token.to_string();

            if (!rule->block())
                return {};

            auto child_tokens = TokenStream { rule->block()->values() };

            Vector<JS::NonnullGCPtr<CSSKeyframeRule>> keyframes;
            while (child_tokens.has_next_token()) {
                child_tokens.skip_whitespace();
                // keyframe-selector = <keyframe-keyword> | <percentage>
                // keyframe-keyword = "from" | "to"
                // selector = <keyframe-selector>#
                // keyframes-block = "{" <declaration-list>? "}"
                // keyframe-rule = <selector> <keyframes-block>

                auto selectors = Vector<CSS::Percentage> {};
                while (child_tokens.has_next_token()) {
                    child_tokens.skip_whitespace();
                    if (!child_tokens.has_next_token())
                        break;
                    auto tok = child_tokens.next_token();
                    if (!tok.is_token()) {
                        dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @keyframes rule has invalid selector: {}; discarding.", tok.to_debug_string());
                        child_tokens.reconsume_current_input_token();
                        break;
                    }
                    auto token = tok.token();
                    auto read_a_selector = false;
                    if (token.is(Token::Type::Ident)) {
                        if (token.ident().equals_ignoring_ascii_case("from"sv)) {
                            selectors.append(CSS::Percentage(0));
                            read_a_selector = true;
                        }
                        if (token.ident().equals_ignoring_ascii_case("to"sv)) {
                            selectors.append(CSS::Percentage(100));
                            read_a_selector = true;
                        }
                    } else if (token.is(Token::Type::Percentage)) {
                        selectors.append(CSS::Percentage(token.percentage()));
                        read_a_selector = true;
                    }

                    if (read_a_selector) {
                        child_tokens.skip_whitespace();
                        if (child_tokens.next_token().is(Token::Type::Comma))
                            continue;
                    }

                    child_tokens.reconsume_current_input_token();
                    break;
                }

                if (!child_tokens.has_next_token())
                    break;

                child_tokens.skip_whitespace();
                auto token = child_tokens.next_token();
                if (token.is_block()) {
                    auto block_tokens = token.block().values();
                    auto block_stream = TokenStream { block_tokens };

                    auto block_declarations = parse_a_list_of_declarations(block_stream);
                    auto style = convert_to_style_declaration(block_declarations);
                    for (auto& selector : selectors) {
                        auto keyframe_rule = CSSKeyframeRule::create(m_context.realm(), selector, *style);
                        keyframes.append(keyframe_rule);
                    }
                } else {
                    dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @keyframes rule has invalid block: {}; discarding.", token.to_debug_string());
                }
            }

            return CSSKeyframesRule::create(m_context.realm(), name, move(keyframes));
        }
        if (rule->at_rule_name().equals_ignoring_ascii_case("namespace"sv)) {
            // https://drafts.csswg.org/css-namespaces/#syntax
            auto token_stream = TokenStream { rule->prelude() };
            token_stream.skip_whitespace();

            auto token = token_stream.next_token();
            Optional<DeprecatedString> prefix = {};
            if (token.is(Token::Type::Ident)) {
                prefix = token.token().ident();
                token_stream.skip_whitespace();
                token = token_stream.next_token();
            }

            DeprecatedString namespace_uri;
            if (token.is(Token::Type::String)) {
                namespace_uri = token.token().string();
            } else if (auto url = parse_url_function(token); url.has_value()) {
                namespace_uri = url.value().to_deprecated_string();
            } else {
                dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @namespace rule invalid; discarding.");
                return {};
            }

            token_stream.skip_whitespace();
            if (token_stream.has_next_token()) {
                dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @namespace rule invalid; discarding.");
                return {};
            }

            return CSSNamespaceRule::create(m_context.realm(), prefix, namespace_uri);
        }

        // FIXME: More at rules!
        dbgln_if(CSS_PARSER_DEBUG, "Unrecognized CSS at-rule: @{}", rule->at_rule_name());
        return {};
    }

    auto prelude_stream = TokenStream(rule->prelude());
    auto selectors = parse_a_selector_list(prelude_stream, SelectorType::Standalone);

    if (selectors.is_error()) {
        if (selectors.error() == ParseError::SyntaxError) {
            dbgln_if(CSS_PARSER_DEBUG, "CSSParser: style rule selectors invalid; discarding.");
            if constexpr (CSS_PARSER_DEBUG) {
                prelude_stream.dump_all_tokens();
            }
        }
        return {};
    }

    if (selectors.value().is_empty()) {
        dbgln_if(CSS_PARSER_DEBUG, "CSSParser: empty selector; discarding.");
        return {};
    }

    if (!rule->block()->is_curly())
        return {};

    auto stream = TokenStream(rule->block()->values());
    auto declarations_and_at_rules = parse_a_style_blocks_contents(stream);

    auto* declaration = convert_to_style_declaration(declarations_and_at_rules);
    if (!declaration) {
        dbgln_if(CSS_PARSER_DEBUG, "CSSParser: style rule declaration invalid; discarding.");
        return {};
    }

    return CSSStyleRule::create(m_context.realm(), move(selectors.value()), *declaration);
}

auto Parser::extract_properties(Vector<DeclarationOrAtRule> const& declarations_and_at_rules) -> PropertiesAndCustomProperties
{
    PropertiesAndCustomProperties result;
    for (auto const& declaration_or_at_rule : declarations_and_at_rules) {
        if (declaration_or_at_rule.is_at_rule()) {
            dbgln_if(CSS_PARSER_DEBUG, "!!! CSS at-rule is not allowed here!");
            continue;
        }

        auto const& declaration = declaration_or_at_rule.declaration();

        if (auto maybe_property = convert_to_style_property(declaration); maybe_property.has_value()) {
            auto property = maybe_property.release_value();
            if (property.property_id == PropertyID::Custom) {
                result.custom_properties.set(property.custom_name, property);
            } else {
                result.properties.append(move(property));
            }
        }
    }
    return result;
}

PropertyOwningCSSStyleDeclaration* Parser::convert_to_style_declaration(Vector<DeclarationOrAtRule> const& declarations_and_at_rules)
{
    auto [properties, custom_properties] = extract_properties(declarations_and_at_rules);
    return PropertyOwningCSSStyleDeclaration::create(m_context.realm(), move(properties), move(custom_properties));
}

Optional<StyleProperty> Parser::convert_to_style_property(Declaration const& declaration)
{
    auto property_name = declaration.name();
    auto property_id = property_id_from_string(property_name);

    if (!property_id.has_value()) {
        if (property_name.starts_with("--"sv)) {
            property_id = PropertyID::Custom;
        } else if (has_ignored_vendor_prefix(property_name)) {
            return {};
        } else if (!property_name.starts_with('-')) {
            dbgln_if(CSS_PARSER_DEBUG, "Unrecognized CSS property '{}'", property_name);
            return {};
        }
    }

    auto value_token_stream = TokenStream(declaration.values());
    auto value = parse_css_value(property_id.value(), value_token_stream);
    if (value.is_error()) {
        if (value.error() == ParseError::SyntaxError) {
            dbgln_if(CSS_PARSER_DEBUG, "Unable to parse value for CSS property '{}'.", property_name);
            if constexpr (CSS_PARSER_DEBUG) {
                value_token_stream.dump_all_tokens();
            }
        }
        return {};
    }

    if (property_id.value() == PropertyID::Custom)
        return StyleProperty { declaration.importance(), property_id.value(), value.release_value(), declaration.name() };

    return StyleProperty { declaration.importance(), property_id.value(), value.release_value(), {} };
}

RefPtr<StyleValue> Parser::parse_builtin_value(ComponentValue const& component_value)
{
    if (component_value.is(Token::Type::Ident)) {
        auto ident = component_value.token().ident();
        if (ident.equals_ignoring_ascii_case("inherit"sv))
            return InheritStyleValue::the();
        if (ident.equals_ignoring_ascii_case("initial"sv))
            return InitialStyleValue::the();
        if (ident.equals_ignoring_ascii_case("unset"sv))
            return UnsetStyleValue::the();
        if (ident.equals_ignoring_ascii_case("revert"sv))
            return RevertStyleValue::the();
        // FIXME: Implement `revert-layer` from CSS-CASCADE-5.
    }

    return nullptr;
}

RefPtr<CalculatedStyleValue> Parser::parse_calculated_value(ComponentValue const& component_value)
{
    if (!component_value.is_function())
        return nullptr;

    auto const& function = component_value.function();

    auto function_node = parse_a_calc_function_node(function);
    if (!function_node)
        return nullptr;

    auto function_type = function_node->determine_type(m_context.current_property_id());
    if (!function_type.has_value())
        return nullptr;

    return CalculatedStyleValue::create(function_node.release_nonnull(), function_type.release_value());
}

OwnPtr<CalculationNode> Parser::parse_a_calc_function_node(Function const& function)
{
    if (function.name().equals_ignoring_ascii_case("calc"sv))
        return parse_a_calculation(function.values());

    if (auto maybe_function = parse_math_function(m_context.current_property_id(), function))
        return maybe_function;

    return nullptr;
}

Optional<Dimension> Parser::parse_dimension(ComponentValue const& component_value)
{
    if (component_value.is(Token::Type::Dimension)) {
        auto numeric_value = component_value.token().dimension_value();
        auto unit_string = component_value.token().dimension_unit();

        if (auto length_type = Length::unit_from_name(unit_string); length_type.has_value())
            return Length { numeric_value, length_type.release_value() };

        if (auto angle_type = Angle::unit_from_name(unit_string); angle_type.has_value())
            return Angle { numeric_value, angle_type.release_value() };

        if (auto frequency_type = Frequency::unit_from_name(unit_string); frequency_type.has_value())
            return Frequency { numeric_value, frequency_type.release_value() };

        if (auto resolution_type = Resolution::unit_from_name(unit_string); resolution_type.has_value())
            return Resolution { numeric_value, resolution_type.release_value() };

        if (auto time_type = Time::unit_from_name(unit_string); time_type.has_value())
            return Time { numeric_value, time_type.release_value() };
    }

    if (component_value.is(Token::Type::Percentage))
        return Percentage { component_value.token().percentage() };

    if (component_value.is(Token::Type::Number)) {
        auto numeric_value = component_value.token().number_value();
        if (numeric_value == 0)
            return Length::make_px(0);
        if (m_context.in_quirks_mode() && property_has_quirk(m_context.current_property_id(), Quirk::UnitlessLength)) {
            // https://quirks.spec.whatwg.org/#quirky-length-value
            // FIXME: Disallow quirk when inside a CSS sub-expression (like `calc()`)
            // "The <quirky-length> value must not be supported in arguments to CSS expressions other than the rect()
            // expression, and must not be supported in the supports() static method of the CSS interface."
            return Length::make_px(CSSPixels::nearest_value_for(numeric_value));
        }
    }

    return {};
}

Optional<LengthOrCalculated> Parser::parse_source_size_value(ComponentValue const& component_value)
{
    if (component_value.is(Token::Type::Ident) && component_value.token().ident().equals_ignoring_ascii_case("auto"sv)) {
        return LengthOrCalculated { Length::make_auto() };
    }

    if (auto calculated_value = parse_calculated_value(component_value)) {
        return LengthOrCalculated { calculated_value.release_nonnull() };
    }

    if (auto length = parse_length(component_value); length.has_value()) {
        return LengthOrCalculated { length.release_value() };
    }

    return {};
}

Optional<Length> Parser::parse_length(ComponentValue const& component_value)
{
    auto dimension = parse_dimension(component_value);
    if (!dimension.has_value())
        return {};

    if (dimension->is_length())
        return dimension->length();

    return {};
}

Optional<Ratio> Parser::parse_ratio(TokenStream<ComponentValue>& tokens)
{
    auto transaction = tokens.begin_transaction();
    tokens.skip_whitespace();

    auto read_number_value = [this](ComponentValue const& component_value) -> Optional<double> {
        if (component_value.is(Token::Type::Number)) {
            return component_value.token().number_value();
        } else if (component_value.is_function()) {
            auto maybe_calc = parse_calculated_value(component_value);
            if (!maybe_calc || !maybe_calc->resolves_to_number())
                return {};
            if (auto resolved_number = maybe_calc->resolve_number(); resolved_number.has_value() && resolved_number.value() >= 0) {
                return resolved_number.value();
            }
        }
        return {};
    };

    // `<ratio> = <number [0,∞]> [ / <number [0,∞]> ]?`
    auto maybe_numerator = read_number_value(tokens.next_token());
    if (!maybe_numerator.has_value() || maybe_numerator.value() < 0)
        return {};
    auto numerator = maybe_numerator.value();

    {
        auto two_value_transaction = tokens.begin_transaction();
        tokens.skip_whitespace();
        auto solidus = tokens.next_token();
        tokens.skip_whitespace();
        auto maybe_denominator = read_number_value(tokens.next_token());

        if (solidus.is_delim('/') && maybe_denominator.has_value() && maybe_denominator.value() >= 0) {
            auto denominator = maybe_denominator.value();
            // Two-value ratio
            two_value_transaction.commit();
            transaction.commit();
            return Ratio { numerator, denominator };
        }
    }

    // Single-value ratio
    transaction.commit();
    return Ratio { numerator };
}

// https://www.w3.org/TR/css-syntax-3/#urange-syntax
Optional<UnicodeRange> Parser::parse_unicode_range(TokenStream<ComponentValue>& tokens)
{
    auto transaction = tokens.begin_transaction();
    tokens.skip_whitespace();

    // <urange> =
    //  u '+' <ident-token> '?'* |
    //  u <dimension-token> '?'* |
    //  u <number-token> '?'* |
    //  u <number-token> <dimension-token> |
    //  u <number-token> <number-token> |
    //  u '+' '?'+
    // (All with no whitespace in between tokens.)

    // NOTE: Parsing this is different from usual. We take these steps:
    // 1. Match the grammar above against the tokens, concatenating them into a string using their original representation.
    // 2. Then, parse that string according to the spec algorithm.
    // Step 2 is performed by calling the other parse_unicode_range() overload.

    auto is_ending_token = [](ComponentValue const& component_value) {
        return component_value.is(Token::Type::EndOfFile)
            || component_value.is(Token::Type::Comma)
            || component_value.is(Token::Type::Semicolon)
            || component_value.is(Token::Type::Whitespace);
    };

    auto create_unicode_range = [&](StringView text, auto& local_transaction) -> Optional<UnicodeRange> {
        auto maybe_unicode_range = parse_unicode_range(text);
        if (maybe_unicode_range.has_value()) {
            local_transaction.commit();
            transaction.commit();
        }
        return maybe_unicode_range;
    };

    // All options start with 'u'/'U'.
    auto const& u = tokens.next_token();
    if (!(u.is(Token::Type::Ident) && u.token().ident().equals_ignoring_ascii_case("u"sv))) {
        dbgln_if(CSS_PARSER_DEBUG, "CSSParser: <urange> does not start with 'u'");
        return {};
    }

    auto const& second_token = tokens.next_token();

    //  u '+' <ident-token> '?'* |
    //  u '+' '?'+
    if (second_token.is_delim('+')) {
        auto local_transaction = tokens.begin_transaction();
        StringBuilder string_builder;
        string_builder.append(second_token.token().representation());

        auto const& third_token = tokens.next_token();
        if (third_token.is(Token::Type::Ident) || third_token.is_delim('?')) {
            string_builder.append(third_token.token().representation());
            while (tokens.peek_token().is_delim('?'))
                string_builder.append(tokens.next_token().token().representation());
            if (is_ending_token(tokens.peek_token()))
                return create_unicode_range(string_builder.string_view(), local_transaction);
        }
    }

    //  u <dimension-token> '?'*
    if (second_token.is(Token::Type::Dimension)) {
        auto local_transaction = tokens.begin_transaction();
        StringBuilder string_builder;
        string_builder.append(second_token.token().representation());
        while (tokens.peek_token().is_delim('?'))
            string_builder.append(tokens.next_token().token().representation());
        if (is_ending_token(tokens.peek_token()))
            return create_unicode_range(string_builder.string_view(), local_transaction);
    }

    //  u <number-token> '?'* |
    //  u <number-token> <dimension-token> |
    //  u <number-token> <number-token>
    if (second_token.is(Token::Type::Number)) {
        auto local_transaction = tokens.begin_transaction();
        StringBuilder string_builder;
        string_builder.append(second_token.token().representation());

        if (is_ending_token(tokens.peek_token()))
            return create_unicode_range(string_builder.string_view(), local_transaction);

        auto const& third_token = tokens.next_token();
        if (third_token.is_delim('?')) {
            string_builder.append(third_token.token().representation());
            while (tokens.peek_token().is_delim('?'))
                string_builder.append(tokens.next_token().token().representation());
            if (is_ending_token(tokens.peek_token()))
                return create_unicode_range(string_builder.string_view(), local_transaction);
        } else if (third_token.is(Token::Type::Dimension)) {
            string_builder.append(third_token.token().representation());
            if (is_ending_token(tokens.peek_token()))
                return create_unicode_range(string_builder.string_view(), local_transaction);
        } else if (third_token.is(Token::Type::Number)) {
            string_builder.append(third_token.token().representation());
            if (is_ending_token(tokens.peek_token()))
                return create_unicode_range(string_builder.string_view(), local_transaction);
        }
    }

    if constexpr (CSS_PARSER_DEBUG) {
        dbgln("CSSParser: Tokens did not match <urange> grammar.");
        tokens.dump_all_tokens();
    }
    return {};
}

Optional<UnicodeRange> Parser::parse_unicode_range(StringView text)
{
    auto make_valid_unicode_range = [&](u32 start_value, u32 end_value) -> Optional<UnicodeRange> {
        // https://www.w3.org/TR/css-syntax-3/#maximum-allowed-code-point
        constexpr u32 maximum_allowed_code_point = 0x10FFFF;

        // To determine what codepoints the <urange> represents:
        // 1. If end value is greater than the maximum allowed code point,
        //    the <urange> is invalid and a syntax error.
        if (end_value > maximum_allowed_code_point) {
            dbgln_if(CSS_PARSER_DEBUG, "CSSParser: Invalid <urange>: end_value ({}) > maximum ({})", end_value, maximum_allowed_code_point);
            return {};
        }

        // 2. If start value is greater than end value, the <urange> is invalid and a syntax error.
        if (start_value > end_value) {
            dbgln_if(CSS_PARSER_DEBUG, "CSSParser: Invalid <urange>: start_value ({}) > end_value ({})", start_value, end_value);
            return {};
        }

        // 3. Otherwise, the <urange> represents a contiguous range of codepoints from start value to end value, inclusive.
        return UnicodeRange { start_value, end_value };
    };

    // 1. Skipping the first u token, concatenate the representations of all the tokens in the production together.
    //    Let this be text.
    // NOTE: The concatenation is already done by the caller.
    GenericLexer lexer { text };

    // 2. If the first character of text is U+002B PLUS SIGN, consume it.
    //    Otherwise, this is an invalid <urange>, and this algorithm must exit.
    if (lexer.next_is('+')) {
        lexer.consume();
    } else {
        dbgln_if(CSS_PARSER_DEBUG, "CSSParser: Second character of <urange> was not '+'; got: '{}'", lexer.consume());
        return {};
    }

    // 3. Consume as many hex digits from text as possible.
    //    then consume as many U+003F QUESTION MARK (?) code points as possible.
    auto hex_digits = lexer.consume_while(is_ascii_hex_digit);
    auto question_marks = lexer.consume_while([](auto it) { return it == '?'; });
    //    If zero code points were consumed, or more than six code points were consumed,
    //    this is an invalid <urange>, and this algorithm must exit.
    size_t consumed_code_points = hex_digits.length() + question_marks.length();
    if (consumed_code_points == 0 || consumed_code_points > 6) {
        dbgln_if(CSS_PARSER_DEBUG, "CSSParser: <urange> start value had {} digits/?s, expected between 1 and 6.", consumed_code_points);
        return {};
    }
    StringView start_value_code_points { hex_digits.characters_without_null_termination(), consumed_code_points };

    //    If any U+003F QUESTION MARK (?) code points were consumed, then:
    if (question_marks.length() > 0) {
        // 1. If there are any code points left in text, this is an invalid <urange>,
        //    and this algorithm must exit.
        if (lexer.tell_remaining() != 0) {
            dbgln_if(CSS_PARSER_DEBUG, "CSSParser: <urange> invalid; had {} code points left over.", lexer.tell_remaining());
            return {};
        }

        // 2. Interpret the consumed code points as a hexadecimal number,
        //    with the U+003F QUESTION MARK (?) code points replaced by U+0030 DIGIT ZERO (0) code points.
        //    This is the start value.
        auto start_value_string = start_value_code_points.replace("?"sv, "0"sv, ReplaceMode::All);
        auto maybe_start_value = AK::StringUtils::convert_to_uint_from_hex<u32>(start_value_string);
        if (!maybe_start_value.has_value()) {
            dbgln_if(CSS_PARSER_DEBUG, "CSSParser: <urange> ?-converted start value did not parse as hex number.");
            return {};
        }
        u32 start_value = maybe_start_value.release_value();

        // 3. Interpret the consumed code points as a hexadecimal number again,
        //    with the U+003F QUESTION MARK (?) code points replaced by U+0046 LATIN CAPITAL LETTER F (F) code points.
        //    This is the end value.
        auto end_value_string = start_value_code_points.replace("?"sv, "F"sv, ReplaceMode::All);
        auto maybe_end_value = AK::StringUtils::convert_to_uint_from_hex<u32>(end_value_string);
        if (!maybe_end_value.has_value()) {
            dbgln_if(CSS_PARSER_DEBUG, "CSSParser: <urange> ?-converted end value did not parse as hex number.");
            return {};
        }
        u32 end_value = maybe_end_value.release_value();

        // 4. Exit this algorithm.
        return make_valid_unicode_range(start_value, end_value);
    }
    //   Otherwise, interpret the consumed code points as a hexadecimal number. This is the start value.
    auto maybe_start_value = AK::StringUtils::convert_to_uint_from_hex<u32>(start_value_code_points);
    if (!maybe_start_value.has_value()) {
        dbgln_if(CSS_PARSER_DEBUG, "CSSParser: <urange> start value did not parse as hex number.");
        return {};
    }
    u32 start_value = maybe_start_value.release_value();

    // 4. If there are no code points left in text, The end value is the same as the start value.
    //    Exit this algorithm.
    if (lexer.tell_remaining() == 0)
        return make_valid_unicode_range(start_value, start_value);

    // 5. If the next code point in text is U+002D HYPHEN-MINUS (-), consume it.
    if (lexer.next_is('-')) {
        lexer.consume();
    }
    //    Otherwise, this is an invalid <urange>, and this algorithm must exit.
    else {
        dbgln_if(CSS_PARSER_DEBUG, "CSSParser: <urange> start and end values not separated by '-'.");
        return {};
    }

    // 6. Consume as many hex digits as possible from text.
    auto end_hex_digits = lexer.consume_while(is_ascii_hex_digit);

    //   If zero hex digits were consumed, or more than 6 hex digits were consumed,
    //   this is an invalid <urange>, and this algorithm must exit.
    if (end_hex_digits.length() == 0 || end_hex_digits.length() > 6) {
        dbgln_if(CSS_PARSER_DEBUG, "CSSParser: <urange> end value had {} digits, expected between 1 and 6.", end_hex_digits.length());
        return {};
    }

    //   If there are any code points left in text, this is an invalid <urange>, and this algorithm must exit.
    if (lexer.tell_remaining() != 0) {
        dbgln_if(CSS_PARSER_DEBUG, "CSSParser: <urange> invalid; had {} code points left over.", lexer.tell_remaining());
        return {};
    }

    // 7. Interpret the consumed code points as a hexadecimal number. This is the end value.
    auto maybe_end_value = AK::StringUtils::convert_to_uint_from_hex<u32>(end_hex_digits);
    if (!maybe_end_value.has_value()) {
        dbgln_if(CSS_PARSER_DEBUG, "CSSParser: <urange> end value did not parse as hex number.");
        return {};
    }
    u32 end_value = maybe_end_value.release_value();

    return make_valid_unicode_range(start_value, end_value);
}

RefPtr<StyleValue> Parser::parse_dimension_value(ComponentValue const& component_value)
{
    // Numbers with no units can be lengths, in two situations:
    // 1) We're in quirks mode, and it's an integer.
    // 2) It's a 0.
    // We handle case 1 here. Case 2 is handled by NumericStyleValue pretending to be a LengthStyleValue if it is 0.

    if (component_value.is(Token::Type::Number) && component_value.token().number_value() != 0 && !(m_context.in_quirks_mode() && property_has_quirk(m_context.current_property_id(), Quirk::UnitlessLength)))
        return nullptr;

    auto dimension = parse_dimension(component_value);
    if (!dimension.has_value())
        return nullptr;

    if (dimension->is_angle())
        return AngleStyleValue::create(dimension->angle());
    if (dimension->is_frequency())
        return FrequencyStyleValue::create(dimension->frequency());
    if (dimension->is_length())
        return LengthStyleValue::create(dimension->length());
    if (dimension->is_percentage())
        return PercentageStyleValue::create(dimension->percentage());
    if (dimension->is_resolution())
        return ResolutionStyleValue::create(dimension->resolution());
    if (dimension->is_time())
        return TimeStyleValue::create(dimension->time());
    VERIFY_NOT_REACHED();
}

RefPtr<StyleValue> Parser::parse_integer_value(TokenStream<ComponentValue>& tokens)
{
    auto peek_token = tokens.peek_token();
    if (peek_token.is(Token::Type::Number) && peek_token.token().number().is_integer()) {
        (void)tokens.next_token();
        return IntegerStyleValue::create(peek_token.token().number().integer_value());
    }

    return nullptr;
}

RefPtr<StyleValue> Parser::parse_number_value(TokenStream<ComponentValue>& tokens)
{
    auto peek_token = tokens.peek_token();
    if (peek_token.is(Token::Type::Number)) {
        (void)tokens.next_token();
        return NumberStyleValue::create(peek_token.token().number().value());
    }

    return nullptr;
}

RefPtr<StyleValue> Parser::parse_identifier_value(ComponentValue const& component_value)
{
    if (component_value.is(Token::Type::Ident)) {
        auto value_id = value_id_from_string(component_value.token().ident());
        if (value_id.has_value())
            return IdentifierStyleValue::create(value_id.value());
    }

    return nullptr;
}

Optional<Color> Parser::parse_rgb_or_hsl_color(StringView function_name, Vector<ComponentValue> const& component_values)
{
    Token params[4];
    bool legacy_syntax = false;
    auto tokens = TokenStream { component_values };

    tokens.skip_whitespace();
    auto const& component1 = tokens.next_token();

    if (!component1.is(Token::Type::Number)
        && !component1.is(Token::Type::Percentage)
        && !component1.is(Token::Type::Dimension))
        return {};
    params[0] = component1.token();

    tokens.skip_whitespace();
    if (tokens.peek_token().is(Token::Type::Comma)) {
        legacy_syntax = true;
        tokens.next_token();
    }

    tokens.skip_whitespace();
    auto const& component2 = tokens.next_token();
    if (!component2.is(Token::Type::Number) && !component2.is(Token::Type::Percentage))
        return {};
    params[1] = component2.token();

    tokens.skip_whitespace();
    if (legacy_syntax && !tokens.next_token().is(Token::Type::Comma))
        return {};

    tokens.skip_whitespace();
    auto const& component3 = tokens.next_token();
    if (!component3.is(Token::Type::Number) && !component3.is(Token::Type::Percentage))
        return {};
    params[2] = component3.token();

    tokens.skip_whitespace();
    auto const& alpha_separator = tokens.peek_token();
    bool has_comma = alpha_separator.is(Token::Type::Comma);
    bool has_slash = alpha_separator.is_delim('/');
    if (legacy_syntax ? has_comma : has_slash) {
        tokens.next_token();

        tokens.skip_whitespace();
        auto const& component4 = tokens.next_token();
        if (!component4.is(Token::Type::Number) && !component4.is(Token::Type::Percentage))
            return {};
        params[3] = component4.token();
    }

    tokens.skip_whitespace();
    if (tokens.has_next_token())
        return {};

    if (function_name.equals_ignoring_ascii_case("rgb"sv)
        || function_name.equals_ignoring_ascii_case("rgba"sv)) {

        // https://www.w3.org/TR/css-color-4/#rgb-functions

        u8 a_val = 255;
        if (params[3].is(Token::Type::Number))
            a_val = clamp(lround(params[3].number_value() * 255.0), 0, 255);
        else if (params[3].is(Token::Type::Percentage))
            a_val = clamp(lround(params[3].percentage() * 2.55), 0, 255);

        if (params[0].is(Token::Type::Number)
            && params[1].is(Token::Type::Number)
            && params[2].is(Token::Type::Number)) {

            u8 r_val = clamp(llroundf(params[0].number_value()), 0, 255);
            u8 g_val = clamp(llroundf(params[1].number_value()), 0, 255);
            u8 b_val = clamp(llroundf(params[2].number_value()), 0, 255);

            return Color(r_val, g_val, b_val, a_val);
        }

        if (params[0].is(Token::Type::Percentage)
            && params[1].is(Token::Type::Percentage)
            && params[2].is(Token::Type::Percentage)) {

            u8 r_val = lround(clamp(params[0].percentage() * 2.55, 0, 255));
            u8 g_val = lround(clamp(params[1].percentage() * 2.55, 0, 255));
            u8 b_val = lround(clamp(params[2].percentage() * 2.55, 0, 255));

            return Color(r_val, g_val, b_val, a_val);
        }
    } else if (function_name.equals_ignoring_ascii_case("hsl"sv)
        || function_name.equals_ignoring_ascii_case("hsla"sv)) {

        // https://www.w3.org/TR/css-color-4/#the-hsl-notation

        auto a_val = 1.0;
        if (params[3].is(Token::Type::Number))
            a_val = params[3].number_value();
        else if (params[3].is(Token::Type::Percentage))
            a_val = params[3].percentage() / 100.0;

        if (params[0].is(Token::Type::Dimension)
            && params[1].is(Token::Type::Percentage)
            && params[2].is(Token::Type::Percentage)) {

            auto numeric_value = params[0].dimension_value();
            auto unit_string = params[0].dimension_unit();
            auto angle_type = Angle::unit_from_name(unit_string);

            if (!angle_type.has_value())
                return {};

            auto angle = Angle { numeric_value, angle_type.release_value() };

            float h_val = fmod(angle.to_degrees(), 360.0);
            float s_val = params[1].percentage() / 100.0;
            float l_val = params[2].percentage() / 100.0;

            return Color::from_hsla(h_val, s_val, l_val, a_val);
        }

        if (params[0].is(Token::Type::Number)
            && params[1].is(Token::Type::Percentage)
            && params[2].is(Token::Type::Percentage)) {

            float h_val = fmod(params[0].number_value(), 360.0);
            float s_val = params[1].percentage() / 100.0;
            float l_val = params[2].percentage() / 100.0;

            return Color::from_hsla(h_val, s_val, l_val, a_val);
        }
    }

    return {};
}

// https://www.w3.org/TR/CSS2/visufx.html#value-def-shape
RefPtr<StyleValue> Parser::parse_rect_value(ComponentValue const& component_value)
{
    if (!component_value.is_function())
        return nullptr;
    auto const& function = component_value.function();
    if (!function.name().equals_ignoring_ascii_case("rect"sv))
        return nullptr;

    Vector<Length, 4> params;
    auto tokens = TokenStream { function.values() };

    enum class CommaRequirement {
        Unknown,
        RequiresCommas,
        RequiresNoCommas
    };

    enum class Side {
        Top = 0,
        Right = 1,
        Bottom = 2,
        Left = 3
    };

    auto comma_requirement = CommaRequirement::Unknown;

    // In CSS 2.1, the only valid <shape> value is: rect(<top>, <right>, <bottom>, <left>) where
    // <top> and <bottom> specify offsets from the top border edge of the box, and <right>, and
    //  <left> specify offsets from the left border edge of the box.
    for (size_t side = 0; side < 4; side++) {
        tokens.skip_whitespace();

        // <top>, <right>, <bottom>, and <left> may either have a <length> value or 'auto'.
        // Negative lengths are permitted.
        auto current_token = tokens.next_token().token();
        if (current_token.is(Token::Type::Ident) && current_token.ident().equals_ignoring_ascii_case("auto"sv)) {
            params.append(Length::make_auto());
        } else {
            auto maybe_length = parse_length(current_token);
            if (!maybe_length.has_value())
                return nullptr;
            params.append(maybe_length.value());
        }
        tokens.skip_whitespace();

        // The last side, should be no more tokens following it.
        if (static_cast<Side>(side) == Side::Left) {
            if (tokens.has_next_token())
                return nullptr;
            break;
        }

        bool next_is_comma = tokens.peek_token().is(Token::Type::Comma);

        // Authors should separate offset values with commas. User agents must support separation
        // with commas, but may also support separation without commas (but not a combination),
        // because a previous revision of this specification was ambiguous in this respect.
        if (comma_requirement == CommaRequirement::Unknown)
            comma_requirement = next_is_comma ? CommaRequirement::RequiresCommas : CommaRequirement::RequiresNoCommas;

        if (comma_requirement == CommaRequirement::RequiresCommas) {
            if (next_is_comma)
                tokens.next_token();
            else
                return nullptr;
        } else if (comma_requirement == CommaRequirement::RequiresNoCommas) {
            if (next_is_comma)
                return nullptr;
        } else {
            VERIFY_NOT_REACHED();
        }
    }

    return RectStyleValue::create(EdgeRect { params[0], params[1], params[2], params[3] });
}

Optional<Color> Parser::parse_color(ComponentValue const& component_value)
{
    // https://www.w3.org/TR/css-color-4/
    if (component_value.is(Token::Type::Ident)) {
        auto ident = component_value.token().ident();

        auto color = Color::from_string(ident);
        if (color.has_value())
            return color;

    } else if (component_value.is(Token::Type::Hash)) {
        auto color = Color::from_string(DeprecatedString::formatted("#{}", component_value.token().hash_value()));
        if (color.has_value())
            return color;
        return {};

    } else if (component_value.is_function()) {
        auto const& function = component_value.function();
        auto const& values = function.values();

        return parse_rgb_or_hsl_color(function.name(), values);
    }

    // https://quirks.spec.whatwg.org/#the-hashless-hex-color-quirk
    if (m_context.in_quirks_mode() && property_has_quirk(m_context.current_property_id(), Quirk::HashlessHexColor)) {
        // The value of a quirky color is obtained from the possible component values using the following algorithm,
        // aborting on the first step that returns a value:

        // 1. Let cv be the component value.
        auto const& cv = component_value;
        DeprecatedString serialization;
        // 2. If cv is a <number-token> or a <dimension-token>, follow these substeps:
        if (cv.is(Token::Type::Number) || cv.is(Token::Type::Dimension)) {
            // 1. If cv’s type flag is not "integer", return an error.
            //    This means that values that happen to use scientific notation, e.g., 5e5e5e, will fail to parse.
            if (!cv.token().number().is_integer())
                return {};

            // 2. If cv’s value is less than zero, return an error.
            auto value = cv.is(Token::Type::Number) ? cv.token().to_integer() : cv.token().dimension_value_int();
            if (value < 0)
                return {};

            // 3. Let serialization be the serialization of cv’s value, as a base-ten integer using digits 0-9 (U+0030 to U+0039) in the shortest form possible.
            StringBuilder serialization_builder;
            serialization_builder.appendff("{}", value);

            // 4. If cv is a <dimension-token>, append the unit to serialization.
            if (cv.is(Token::Type::Dimension))
                serialization_builder.append(cv.token().dimension_unit());

            // 5. If serialization consists of fewer than six characters, prepend zeros (U+0030) so that it becomes six characters.
            serialization = serialization_builder.to_deprecated_string();
            if (serialization_builder.length() < 6) {
                StringBuilder builder;
                for (size_t i = 0; i < (6 - serialization_builder.length()); i++)
                    builder.append('0');
                builder.append(serialization_builder.string_view());
                serialization = builder.to_deprecated_string();
            }
        }
        // 3. Otherwise, cv is an <ident-token>; let serialization be cv’s value.
        else {
            if (!cv.is(Token::Type::Ident))
                return {};
            serialization = cv.token().ident();
        }

        // 4. If serialization does not consist of three or six characters, return an error.
        if (serialization.length() != 3 && serialization.length() != 6)
            return {};

        // 5. If serialization contains any characters not in the range [0-9A-Fa-f] (U+0030 to U+0039, U+0041 to U+0046, U+0061 to U+0066), return an error.
        for (auto c : serialization) {
            if (!((c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f')))
                return {};
        }

        // 6. Return the concatenation of "#" (U+0023) and serialization.
        DeprecatedString concatenation = DeprecatedString::formatted("#{}", serialization);
        return Color::from_string(concatenation);
    }

    return {};
}

RefPtr<StyleValue> Parser::parse_color_value(ComponentValue const& component_value)
{
    auto color = parse_color(component_value);
    if (color.has_value())
        return ColorStyleValue::create(color.value());

    if (component_value.is(Token::Type::Ident)) {
        auto ident = value_id_from_string(component_value.token().ident());
        if (ident.has_value() && IdentifierStyleValue::is_color(ident.value()))
            return IdentifierStyleValue::create(ident.value());
    }

    return nullptr;
}

RefPtr<StyleValue> Parser::parse_ratio_value(TokenStream<ComponentValue>& tokens)
{
    if (auto ratio = parse_ratio(tokens); ratio.has_value())
        return RatioStyleValue::create(ratio.release_value());
    return nullptr;
}

RefPtr<StyleValue> Parser::parse_string_value(ComponentValue const& component_value)
{
    if (component_value.is(Token::Type::String))
        return StringStyleValue::create(MUST(String::from_utf8(component_value.token().string())));

    return nullptr;
}

RefPtr<StyleValue> Parser::parse_image_value(ComponentValue const& component_value)
{
    auto url = parse_url_function(component_value);
    if (url.has_value())
        return ImageStyleValue::create(url.value());
    auto linear_gradient = parse_linear_gradient_function(component_value);
    if (linear_gradient)
        return linear_gradient;
    auto conic_gradient = parse_conic_gradient_function(component_value);
    if (conic_gradient)
        return conic_gradient;
    return parse_radial_gradient_function(component_value);
}

// https://svgwg.org/svg2-draft/painting.html#SpecifyingPaint
RefPtr<StyleValue> Parser::parse_paint_value(TokenStream<ComponentValue>& tokens)
{
    // `<paint> = none | <color> | <url> [none | <color>]? | context-fill | context-stroke`

    auto parse_color_or_none = [&]() -> Optional<RefPtr<StyleValue>> {
        if (auto color = parse_color_value(tokens.peek_token())) {
            (void)tokens.next_token();
            return color;
        }

        // NOTE: <color> also accepts identifiers, so we do this identifier check last.
        if (tokens.peek_token().is(Token::Type::Ident)) {
            auto maybe_ident = value_id_from_string(tokens.peek_token().token().ident());
            if (maybe_ident.has_value()) {
                // FIXME: Accept `context-fill` and `context-stroke`
                switch (*maybe_ident) {
                case ValueID::None:
                    (void)tokens.next_token();
                    return IdentifierStyleValue::create(*maybe_ident);
                default:
                    return nullptr;
                }
            }
        }

        return OptionalNone {};
    };

    // FIMXE: Allow context-fill/context-stroke here
    if (auto color_or_none = parse_color_or_none(); color_or_none.has_value())
        return *color_or_none;

    if (auto url = parse_url_value(tokens.peek_token())) {
        (void)tokens.next_token();
        tokens.skip_whitespace();
        if (auto color_or_none = parse_color_or_none(); color_or_none == nullptr) {
            // Fail to parse if the fallback is invalid, but otherwise ignore it.
            // FIXME: Use fallback color
            return nullptr;
        }
        return url;
    }

    return nullptr;
}

template<typename ParseFunction>
RefPtr<StyleValue> Parser::parse_comma_separated_value_list(Vector<ComponentValue> const& component_values, ParseFunction parse_one_value)
{
    auto tokens = TokenStream { component_values };
    auto first = parse_one_value(tokens);
    if (!first || !tokens.has_next_token())
        return first;

    StyleValueVector values;
    values.append(first.release_nonnull());

    while (tokens.has_next_token()) {
        if (!tokens.next_token().is(Token::Type::Comma))
            return nullptr;

        if (auto maybe_value = parse_one_value(tokens)) {
            values.append(maybe_value.release_nonnull());
            continue;
        }
        return nullptr;
    }

    return StyleValueList::create(move(values), StyleValueList::Separator::Comma);
}

RefPtr<StyleValue> Parser::parse_simple_comma_separated_value_list(PropertyID property_id, Vector<ComponentValue> const& component_values)
{
    return parse_comma_separated_value_list(component_values, [=, this](auto& tokens) -> RefPtr<StyleValue> {
        if (auto value = parse_css_value_for_property(property_id, tokens))
            return value;
        tokens.reconsume_current_input_token();
        return nullptr;
    });
}

static void remove_property(Vector<PropertyID>& properties, PropertyID property_to_remove)
{
    properties.remove_first_matching([&](auto it) { return it == property_to_remove; });
}

// https://www.w3.org/TR/css-sizing-4/#aspect-ratio
RefPtr<StyleValue> Parser::parse_aspect_ratio_value(Vector<ComponentValue> const& component_values)
{
    // `auto || <ratio>`
    RefPtr<StyleValue> auto_value;
    RefPtr<StyleValue> ratio_value;

    auto tokens = TokenStream { component_values };
    while (tokens.has_next_token()) {
        auto maybe_value = parse_css_value_for_property(PropertyID::AspectRatio, tokens);
        if (!maybe_value)
            return nullptr;

        if (maybe_value->is_ratio()) {
            if (ratio_value)
                return nullptr;
            ratio_value = maybe_value.release_nonnull();
            continue;
        }

        if (maybe_value->is_identifier() && maybe_value->as_identifier().id() == ValueID::Auto) {
            if (auto_value)
                return nullptr;
            auto_value = maybe_value.release_nonnull();
            continue;
        }

        return nullptr;
    }

    if (auto_value && ratio_value) {
        return StyleValueList::create(
            StyleValueVector { auto_value.release_nonnull(), ratio_value.release_nonnull() },
            StyleValueList::Separator::Space);
    }

    if (ratio_value)
        return ratio_value.release_nonnull();

    if (auto_value)
        return auto_value.release_nonnull();

    return nullptr;
}

RefPtr<StyleValue> Parser::parse_background_value(Vector<ComponentValue> const& component_values)
{
    StyleValueVector background_images;
    StyleValueVector background_positions;
    StyleValueVector background_sizes;
    StyleValueVector background_repeats;
    StyleValueVector background_attachments;
    StyleValueVector background_clips;
    StyleValueVector background_origins;
    RefPtr<StyleValue> background_color;

    auto initial_background_image = property_initial_value(m_context.realm(), PropertyID::BackgroundImage);
    auto initial_background_position = property_initial_value(m_context.realm(), PropertyID::BackgroundPosition);
    auto initial_background_size = property_initial_value(m_context.realm(), PropertyID::BackgroundSize);
    auto initial_background_repeat = property_initial_value(m_context.realm(), PropertyID::BackgroundRepeat);
    auto initial_background_attachment = property_initial_value(m_context.realm(), PropertyID::BackgroundAttachment);
    auto initial_background_clip = property_initial_value(m_context.realm(), PropertyID::BackgroundClip);
    auto initial_background_origin = property_initial_value(m_context.realm(), PropertyID::BackgroundOrigin);
    auto initial_background_color = property_initial_value(m_context.realm(), PropertyID::BackgroundColor);

    // Per-layer values
    RefPtr<StyleValue> background_image;
    RefPtr<StyleValue> background_position;
    RefPtr<StyleValue> background_size;
    RefPtr<StyleValue> background_repeat;
    RefPtr<StyleValue> background_attachment;
    RefPtr<StyleValue> background_clip;
    RefPtr<StyleValue> background_origin;

    bool has_multiple_layers = false;
    // BackgroundSize is always parsed as part of BackgroundPosition, so we don't include it here.
    Vector<PropertyID> remaining_layer_properties {
        PropertyID::BackgroundAttachment,
        PropertyID::BackgroundClip,
        PropertyID::BackgroundColor,
        PropertyID::BackgroundImage,
        PropertyID::BackgroundOrigin,
        PropertyID::BackgroundPosition,
        PropertyID::BackgroundRepeat,
    };

    auto background_layer_is_valid = [&](bool allow_background_color) -> bool {
        if (allow_background_color) {
            if (background_color)
                return true;
        } else {
            if (background_color)
                return false;
        }
        return background_image || background_position || background_size || background_repeat || background_attachment || background_clip || background_origin;
    };

    auto complete_background_layer = [&]() {
        background_images.append(background_image ? background_image.release_nonnull() : initial_background_image);
        background_positions.append(background_position ? background_position.release_nonnull() : initial_background_position);
        background_sizes.append(background_size ? background_size.release_nonnull() : initial_background_size);
        background_repeats.append(background_repeat ? background_repeat.release_nonnull() : initial_background_repeat);
        background_attachments.append(background_attachment ? background_attachment.release_nonnull() : initial_background_attachment);

        if (!background_origin && !background_clip) {
            background_origin = initial_background_origin;
            background_clip = initial_background_clip;
        } else if (!background_clip) {
            background_clip = background_origin;
        }
        background_origins.append(background_origin.release_nonnull());
        background_clips.append(background_clip.release_nonnull());

        background_image = nullptr;
        background_position = nullptr;
        background_size = nullptr;
        background_repeat = nullptr;
        background_attachment = nullptr;
        background_clip = nullptr;
        background_origin = nullptr;

        remaining_layer_properties.clear_with_capacity();
        remaining_layer_properties.unchecked_append(PropertyID::BackgroundAttachment);
        remaining_layer_properties.unchecked_append(PropertyID::BackgroundClip);
        remaining_layer_properties.unchecked_append(PropertyID::BackgroundColor);
        remaining_layer_properties.unchecked_append(PropertyID::BackgroundImage);
        remaining_layer_properties.unchecked_append(PropertyID::BackgroundOrigin);
        remaining_layer_properties.unchecked_append(PropertyID::BackgroundPosition);
        remaining_layer_properties.unchecked_append(PropertyID::BackgroundRepeat);
    };

    auto tokens = TokenStream { component_values };
    while (tokens.has_next_token()) {
        if (tokens.peek_token().is(Token::Type::Comma)) {
            has_multiple_layers = true;
            if (!background_layer_is_valid(false))
                return nullptr;
            complete_background_layer();
            (void)tokens.next_token();
            continue;
        }

        auto value_and_property = parse_css_value_for_properties(remaining_layer_properties, tokens);
        if (!value_and_property.has_value())
            return nullptr;
        auto& value = value_and_property->style_value;
        remove_property(remaining_layer_properties, value_and_property->property);

        switch (value_and_property->property) {
        case PropertyID::BackgroundAttachment:
            VERIFY(!background_attachment);
            background_attachment = value.release_nonnull();
            continue;
        case PropertyID::BackgroundColor:
            VERIFY(!background_color);
            background_color = value.release_nonnull();
            continue;
        case PropertyID::BackgroundImage:
            VERIFY(!background_image);
            background_image = value.release_nonnull();
            continue;
        case PropertyID::BackgroundClip:
        case PropertyID::BackgroundOrigin: {
            // background-origin and background-clip accept the same values. From the spec:
            //   "If one <box> value is present then it sets both background-origin and background-clip to that value.
            //    If two values are present, then the first sets background-origin and the second background-clip."
            //        - https://www.w3.org/TR/css-backgrounds-3/#background
            // So, we put the first one in background-origin, then if we get a second, we put it in background-clip.
            // If we only get one, we copy the value before creating the BackgroundStyleValue.
            if (!background_origin) {
                background_origin = value.release_nonnull();
            } else if (!background_clip) {
                background_clip = value.release_nonnull();
            } else {
                VERIFY_NOT_REACHED();
            }
            continue;
        }
        case PropertyID::BackgroundPosition: {
            VERIFY(!background_position);
            tokens.reconsume_current_input_token();
            if (auto maybe_background_position = parse_single_background_position_value(tokens)) {
                background_position = maybe_background_position.release_nonnull();

                // Attempt to parse `/ <background-size>`
                auto transaction = tokens.begin_transaction();
                auto& maybe_slash = tokens.next_token();
                if (maybe_slash.is_delim('/')) {
                    if (auto maybe_background_size = parse_single_background_size_value(tokens)) {
                        transaction.commit();
                        background_size = maybe_background_size.release_nonnull();
                        continue;
                    }
                    return nullptr;
                }
                continue;
            }
            return nullptr;
        }
        case PropertyID::BackgroundRepeat: {
            VERIFY(!background_repeat);
            tokens.reconsume_current_input_token();
            if (auto maybe_repeat = parse_single_background_repeat_value(tokens)) {
                background_repeat = maybe_repeat.release_nonnull();
                continue;
            }
            return nullptr;
        }
        default:
            VERIFY_NOT_REACHED();
        }

        return nullptr;
    }

    if (!background_layer_is_valid(true))
        return nullptr;

    // We only need to create StyleValueLists if there are multiple layers.
    // Otherwise, we can pass the single StyleValues directly.
    if (has_multiple_layers) {
        complete_background_layer();

        if (!background_color)
            background_color = initial_background_color;
        return BackgroundStyleValue::create(
            background_color.release_nonnull(),
            StyleValueList::create(move(background_images), StyleValueList::Separator::Comma),
            StyleValueList::create(move(background_positions), StyleValueList::Separator::Comma),
            StyleValueList::create(move(background_sizes), StyleValueList::Separator::Comma),
            StyleValueList::create(move(background_repeats), StyleValueList::Separator::Comma),
            StyleValueList::create(move(background_attachments), StyleValueList::Separator::Comma),
            StyleValueList::create(move(background_origins), StyleValueList::Separator::Comma),
            StyleValueList::create(move(background_clips), StyleValueList::Separator::Comma));
    }

    if (!background_color)
        background_color = initial_background_color;
    if (!background_image)
        background_image = initial_background_image;
    if (!background_position)
        background_position = initial_background_position;
    if (!background_size)
        background_size = initial_background_size;
    if (!background_repeat)
        background_repeat = initial_background_repeat;
    if (!background_attachment)
        background_attachment = initial_background_attachment;

    if (!background_origin && !background_clip) {
        background_origin = initial_background_origin;
        background_clip = initial_background_clip;
    } else if (!background_clip) {
        background_clip = background_origin;
    }

    return BackgroundStyleValue::create(
        background_color.release_nonnull(),
        background_image.release_nonnull(),
        background_position.release_nonnull(),
        background_size.release_nonnull(),
        background_repeat.release_nonnull(),
        background_attachment.release_nonnull(),
        background_origin.release_nonnull(),
        background_clip.release_nonnull());
}

static Optional<PositionEdge> identifier_to_edge(ValueID identifier)
{
    switch (identifier) {
    case ValueID::Top:
        return PositionEdge::Top;
    case ValueID::Bottom:
        return PositionEdge::Bottom;
    case ValueID::Left:
        return PositionEdge::Left;
    case ValueID::Right:
        return PositionEdge::Right;
    default:
        return {};
    }
}

static Optional<LengthPercentage> style_value_to_length_percentage(auto value)
{
    if (value->is_percentage())
        return LengthPercentage { value->as_percentage().percentage() };
    if (value->is_length())
        return LengthPercentage { value->as_length().length() };
    if (value->is_calculated())
        return LengthPercentage { value->as_calculated() };
    return {};
}

RefPtr<StyleValue> Parser::parse_single_background_position_value(TokenStream<ComponentValue>& tokens)
{
    // NOTE: This *looks* like it parses a <position>, but it doesn't. From the spec:
    //      "Note: The background-position property also accepts a three-value syntax.
    //       This has been disallowed generically because it creates parsing ambiguities
    //       when combined with other length or percentage components in a property value."
    //           - https://www.w3.org/TR/css-values-4/#typedef-position
    //       So, we'll need a separate function to parse <position> later.

    auto transaction = tokens.begin_transaction();

    auto is_horizontal = [](ValueID identifier) -> bool {
        switch (identifier) {
        case ValueID::Left:
        case ValueID::Right:
            return true;
        default:
            return false;
        }
    };
    auto is_vertical = [](ValueID identifier) -> bool {
        switch (identifier) {
        case ValueID::Top:
        case ValueID::Bottom:
            return true;
        default:
            return false;
        }
    };

    struct EdgeOffset {
        PositionEdge edge;
        LengthPercentage offset;
        bool edge_provided;
        bool offset_provided;
    };

    Optional<EdgeOffset> horizontal;
    Optional<EdgeOffset> vertical;
    bool found_center = false;

    auto const center_offset = Percentage { 50 };
    auto const zero_offset = Length::make_px(0);

    while (tokens.has_next_token()) {
        // Check if we're done
        auto seen_items = (horizontal.has_value() ? 1 : 0) + (vertical.has_value() ? 1 : 0) + (found_center ? 1 : 0);
        if (seen_items == 2)
            break;

        auto maybe_value = parse_css_value_for_property(PropertyID::BackgroundPosition, tokens);
        if (!maybe_value)
            break;
        auto value = maybe_value.release_nonnull();

        if (auto offset = style_value_to_length_percentage(value); offset.has_value()) {
            if (!horizontal.has_value()) {
                horizontal = EdgeOffset { PositionEdge::Left, *offset, false, true };
            } else if (!vertical.has_value()) {
                vertical = EdgeOffset { PositionEdge::Top, *offset, false, true };
            } else {
                return nullptr;
            }
            continue;
        }

        auto try_parse_offset = [&](bool& offset_provided) -> LengthPercentage {
            auto transaction = tokens.begin_transaction();
            if (tokens.has_next_token()) {
                auto maybe_value = parse_css_value_for_property(PropertyID::BackgroundPosition, tokens);
                if (!maybe_value)
                    return zero_offset;
                auto offset = style_value_to_length_percentage(maybe_value.release_nonnull());
                if (offset.has_value()) {
                    offset_provided = true;
                    transaction.commit();
                    return *offset;
                }
            }
            return zero_offset;
        };

        if (value->is_identifier()) {
            auto identifier = value->to_identifier();
            if (is_horizontal(identifier)) {
                bool offset_provided = false;
                auto offset = try_parse_offset(offset_provided);
                horizontal = EdgeOffset { *identifier_to_edge(identifier), offset, true, offset_provided };
            } else if (is_vertical(identifier)) {
                bool offset_provided = false;
                auto offset = try_parse_offset(offset_provided);
                vertical = EdgeOffset { *identifier_to_edge(identifier), offset, true, offset_provided };
            } else if (identifier == ValueID::Center) {
                found_center = true;
            } else {
                return nullptr;
            }
            continue;
        }

        tokens.reconsume_current_input_token();
        break;
    }

    if (found_center) {
        if (horizontal.has_value() && vertical.has_value())
            return nullptr;
        if (!horizontal.has_value())
            horizontal = EdgeOffset { PositionEdge::Left, center_offset, true, false };
        if (!vertical.has_value())
            vertical = EdgeOffset { PositionEdge::Top, center_offset, true, false };
    }

    if (!horizontal.has_value() && !vertical.has_value())
        return nullptr;

    // Unpack `<edge> <length>`:
    // The loop above reads this pattern as a single EdgeOffset, when actually, it should be treated
    // as `x y` if the edge is horizontal, and `y` (with the second token reconsumed) otherwise.
    if (!vertical.has_value() && horizontal->edge_provided && horizontal->offset_provided) {
        // Split into `x y`
        vertical = EdgeOffset { PositionEdge::Top, horizontal->offset, false, true };
        horizontal->offset = zero_offset;
        horizontal->offset_provided = false;
    } else if (!horizontal.has_value() && vertical->edge_provided && vertical->offset_provided) {
        // `y`, reconsume
        vertical->offset = zero_offset;
        vertical->offset_provided = false;
        tokens.reconsume_current_input_token();
    }

    // If only one value is specified, the second value is assumed to be center.
    if (!horizontal.has_value())
        horizontal = EdgeOffset { PositionEdge::Left, center_offset, false, false };
    if (!vertical.has_value())
        vertical = EdgeOffset { PositionEdge::Top, center_offset, false, false };

    transaction.commit();
    return PositionStyleValue::create(
        EdgeStyleValue::create(horizontal->edge, horizontal->offset),
        EdgeStyleValue::create(vertical->edge, vertical->offset));
}

RefPtr<StyleValue> Parser::parse_single_background_position_x_or_y_value(TokenStream<ComponentValue>& tokens, PropertyID property)
{
    PositionEdge relative_edge {};
    if (property == PropertyID::BackgroundPositionX) {
        // [ center | [ [ left | right | x-start | x-end ]? <length-percentage>? ]! ]#
        relative_edge = PositionEdge::Left;
    } else if (property == PropertyID::BackgroundPositionY) {
        // [ center | [ [ top | bottom | y-start | y-end ]? <length-percentage>? ]! ]#
        relative_edge = PositionEdge::Top;
    } else {
        VERIFY_NOT_REACHED();
    }

    auto transaction = tokens.begin_transaction();
    if (!tokens.has_next_token())
        return nullptr;

    auto value = parse_css_value_for_property(property, tokens);
    if (!value)
        return nullptr;

    if (value->is_identifier()) {
        auto identifier = value->to_identifier();
        if (identifier == ValueID::Center) {
            transaction.commit();
            return EdgeStyleValue::create(relative_edge, Percentage { 50 });
        }
        if (auto edge = identifier_to_edge(identifier); edge.has_value()) {
            relative_edge = *edge;
        } else {
            return nullptr;
        }
        if (tokens.has_next_token()) {
            value = parse_css_value_for_property(property, tokens);
            if (!value) {
                transaction.commit();
                return EdgeStyleValue::create(relative_edge, Length::make_px(0));
            }
        }
    }

    auto offset = style_value_to_length_percentage(value);
    if (offset.has_value()) {
        transaction.commit();
        return EdgeStyleValue::create(relative_edge, *offset);
    }

    // If no offset is provided create this element but with an offset of default value of zero
    transaction.commit();
    return EdgeStyleValue::create(relative_edge, Length::make_px(0));
}

RefPtr<StyleValue> Parser::parse_single_background_repeat_value(TokenStream<ComponentValue>& tokens)
{
    auto transaction = tokens.begin_transaction();

    auto is_directional_repeat = [](StyleValue const& value) -> bool {
        auto value_id = value.to_identifier();
        return value_id == ValueID::RepeatX || value_id == ValueID::RepeatY;
    };

    auto as_repeat = [](ValueID identifier) -> Optional<Repeat> {
        switch (identifier) {
        case ValueID::NoRepeat:
            return Repeat::NoRepeat;
        case ValueID::Repeat:
            return Repeat::Repeat;
        case ValueID::Round:
            return Repeat::Round;
        case ValueID::Space:
            return Repeat::Space;
        default:
            return {};
        }
    };

    auto maybe_x_value = parse_css_value_for_property(PropertyID::BackgroundRepeat, tokens);
    if (!maybe_x_value)
        return nullptr;
    auto x_value = maybe_x_value.release_nonnull();

    if (is_directional_repeat(*x_value)) {
        auto value_id = x_value->to_identifier();
        transaction.commit();
        return BackgroundRepeatStyleValue::create(
            value_id == ValueID::RepeatX ? Repeat::Repeat : Repeat::NoRepeat,
            value_id == ValueID::RepeatX ? Repeat::NoRepeat : Repeat::Repeat);
    }

    auto x_repeat = as_repeat(x_value->to_identifier());
    if (!x_repeat.has_value())
        return nullptr;

    // See if we have a second value for Y
    auto maybe_y_value = parse_css_value_for_property(PropertyID::BackgroundRepeat, tokens);
    if (!maybe_y_value) {
        // We don't have a second value, so use x for both
        transaction.commit();
        return BackgroundRepeatStyleValue::create(x_repeat.value(), x_repeat.value());
    }
    auto y_value = maybe_y_value.release_nonnull();
    if (is_directional_repeat(*y_value))
        return nullptr;

    auto y_repeat = as_repeat(y_value->to_identifier());
    if (!y_repeat.has_value())
        return nullptr;

    transaction.commit();
    return BackgroundRepeatStyleValue::create(x_repeat.value(), y_repeat.value());
}

RefPtr<StyleValue> Parser::parse_single_background_size_value(TokenStream<ComponentValue>& tokens)
{
    auto transaction = tokens.begin_transaction();

    auto get_length_percentage = [](StyleValue& style_value) -> Optional<LengthPercentage> {
        if (style_value.has_auto())
            return LengthPercentage { Length::make_auto() };
        if (style_value.is_percentage())
            return LengthPercentage { style_value.as_percentage().percentage() };
        if (style_value.is_length())
            return LengthPercentage { style_value.as_length().length() };
        return {};
    };

    auto maybe_x_value = parse_css_value_for_property(PropertyID::BackgroundSize, tokens);
    if (!maybe_x_value)
        return nullptr;
    auto x_value = maybe_x_value.release_nonnull();

    if (x_value->to_identifier() == ValueID::Cover || x_value->to_identifier() == ValueID::Contain) {
        transaction.commit();
        return x_value;
    }

    auto maybe_y_value = parse_css_value_for_property(PropertyID::BackgroundSize, tokens);
    if (!maybe_y_value) {
        auto y_value = LengthPercentage { Length::make_auto() };
        auto x_size = get_length_percentage(*x_value);
        if (!x_size.has_value())
            return nullptr;

        transaction.commit();
        return BackgroundSizeStyleValue::create(x_size.value(), y_value);
    }

    auto y_value = maybe_y_value.release_nonnull();
    auto x_size = get_length_percentage(*x_value);
    auto y_size = get_length_percentage(*y_value);

    if (!x_size.has_value() || !y_size.has_value())
        return nullptr;

    transaction.commit();
    return BackgroundSizeStyleValue::create(x_size.release_value(), y_size.release_value());
}

RefPtr<StyleValue> Parser::parse_border_value(Vector<ComponentValue> const& component_values)
{
    if (component_values.size() > 3)
        return nullptr;

    RefPtr<StyleValue> border_width;
    RefPtr<StyleValue> border_color;
    RefPtr<StyleValue> border_style;

    auto remaining_longhands = Vector { PropertyID::BorderWidth, PropertyID::BorderColor, PropertyID::BorderStyle };

    auto tokens = TokenStream { component_values };
    while (tokens.has_next_token()) {
        auto property_and_value = parse_css_value_for_properties(remaining_longhands, tokens);
        if (!property_and_value.has_value())
            return nullptr;
        auto& value = property_and_value->style_value;
        remove_property(remaining_longhands, property_and_value->property);

        switch (property_and_value->property) {
        case PropertyID::BorderWidth: {
            VERIFY(!border_width);
            border_width = value.release_nonnull();
            continue;
        }
        case PropertyID::BorderColor: {
            VERIFY(!border_color);
            border_color = value.release_nonnull();
            continue;
        }
        case PropertyID::BorderStyle: {
            VERIFY(!border_style);
            border_style = value.release_nonnull();
            continue;
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }

    if (!border_width)
        border_width = property_initial_value(m_context.realm(), PropertyID::BorderWidth);
    if (!border_style)
        border_style = property_initial_value(m_context.realm(), PropertyID::BorderStyle);
    if (!border_color)
        border_color = property_initial_value(m_context.realm(), PropertyID::BorderColor);

    return BorderStyleValue::create(border_width.release_nonnull(), border_style.release_nonnull(), border_color.release_nonnull());
}

RefPtr<StyleValue> Parser::parse_border_radius_value(Vector<ComponentValue> const& component_values)
{
    if (component_values.size() == 2) {
        auto horizontal = parse_dimension(component_values[0]);
        auto vertical = parse_dimension(component_values[1]);
        if (horizontal.has_value() && horizontal->is_length_percentage() && vertical.has_value() && vertical->is_length_percentage())
            return BorderRadiusStyleValue::create(horizontal->length_percentage(), vertical->length_percentage());

        return nullptr;
    }

    if (component_values.size() == 1) {
        auto radius = parse_dimension(component_values[0]);
        if (radius.has_value() && radius->is_length_percentage())
            return BorderRadiusStyleValue::create(radius->length_percentage(), radius->length_percentage());
        return nullptr;
    }

    return nullptr;
}

RefPtr<StyleValue> Parser::parse_border_radius_shorthand_value(Vector<ComponentValue> const& component_values)
{
    auto top_left = [&](Vector<LengthPercentage>& radii) { return radii[0]; };
    auto top_right = [&](Vector<LengthPercentage>& radii) {
        switch (radii.size()) {
        case 4:
        case 3:
        case 2:
            return radii[1];
        case 1:
            return radii[0];
        default:
            VERIFY_NOT_REACHED();
        }
    };
    auto bottom_right = [&](Vector<LengthPercentage>& radii) {
        switch (radii.size()) {
        case 4:
        case 3:
            return radii[2];
        case 2:
        case 1:
            return radii[0];
        default:
            VERIFY_NOT_REACHED();
        }
    };
    auto bottom_left = [&](Vector<LengthPercentage>& radii) {
        switch (radii.size()) {
        case 4:
            return radii[3];
        case 3:
        case 2:
            return radii[1];
        case 1:
            return radii[0];
        default:
            VERIFY_NOT_REACHED();
        }
    };

    Vector<LengthPercentage> horizontal_radii;
    Vector<LengthPercentage> vertical_radii;
    bool reading_vertical = false;

    for (auto const& value : component_values) {
        if (value.is_delim('/')) {
            if (reading_vertical || horizontal_radii.is_empty())
                return nullptr;

            reading_vertical = true;
            continue;
        }

        auto maybe_dimension = parse_dimension(value);
        if (!maybe_dimension.has_value() || !maybe_dimension->is_length_percentage())
            return nullptr;
        if (reading_vertical) {
            vertical_radii.append(maybe_dimension->length_percentage());
        } else {
            horizontal_radii.append(maybe_dimension->length_percentage());
        }
    }

    if (horizontal_radii.size() > 4 || vertical_radii.size() > 4
        || horizontal_radii.is_empty()
        || (reading_vertical && vertical_radii.is_empty()))
        return nullptr;

    auto top_left_radius = BorderRadiusStyleValue::create(top_left(horizontal_radii),
        vertical_radii.is_empty() ? top_left(horizontal_radii) : top_left(vertical_radii));
    auto top_right_radius = BorderRadiusStyleValue::create(top_right(horizontal_radii),
        vertical_radii.is_empty() ? top_right(horizontal_radii) : top_right(vertical_radii));
    auto bottom_right_radius = BorderRadiusStyleValue::create(bottom_right(horizontal_radii),
        vertical_radii.is_empty() ? bottom_right(horizontal_radii) : bottom_right(vertical_radii));
    auto bottom_left_radius = BorderRadiusStyleValue::create(bottom_left(horizontal_radii),
        vertical_radii.is_empty() ? bottom_left(horizontal_radii) : bottom_left(vertical_radii));

    return BorderRadiusShorthandStyleValue::create(move(top_left_radius), move(top_right_radius), move(bottom_right_radius), move(bottom_left_radius));
}

RefPtr<StyleValue> Parser::parse_shadow_value(Vector<ComponentValue> const& component_values, AllowInsetKeyword allow_inset_keyword)
{
    // "none"
    if (component_values.size() == 1 && component_values.first().is(Token::Type::Ident)) {
        auto ident = parse_identifier_value(component_values.first());
        if (ident && ident->to_identifier() == ValueID::None)
            return ident;
    }

    return parse_comma_separated_value_list(component_values, [this, allow_inset_keyword](auto& tokens) {
        return parse_single_shadow_value(tokens, allow_inset_keyword);
    });
}

RefPtr<StyleValue> Parser::parse_single_shadow_value(TokenStream<ComponentValue>& tokens, AllowInsetKeyword allow_inset_keyword)
{
    auto transaction = tokens.begin_transaction();

    Optional<Color> color;
    RefPtr<StyleValue> offset_x;
    RefPtr<StyleValue> offset_y;
    RefPtr<StyleValue> blur_radius;
    RefPtr<StyleValue> spread_distance;
    Optional<ShadowPlacement> placement;

    auto possibly_dynamic_length = [&](ComponentValue const& token) -> RefPtr<StyleValue> {
        if (auto calculated_value = parse_calculated_value(token)) {
            if (!calculated_value->resolves_to_length())
                return nullptr;
            return calculated_value;
        }
        auto maybe_length = parse_length(token);
        if (!maybe_length.has_value())
            return nullptr;
        return LengthStyleValue::create(maybe_length.release_value());
    };

    while (tokens.has_next_token()) {
        auto const& token = tokens.peek_token();

        if (auto maybe_color = parse_color(token); maybe_color.has_value()) {
            if (color.has_value())
                return nullptr;
            color = maybe_color.release_value();
            tokens.next_token();
            continue;
        }

        if (auto maybe_offset_x = possibly_dynamic_length(token); maybe_offset_x) {
            // horizontal offset
            if (offset_x)
                return nullptr;
            offset_x = maybe_offset_x;
            tokens.next_token();

            // vertical offset
            if (!tokens.has_next_token())
                return nullptr;
            auto maybe_offset_y = possibly_dynamic_length(tokens.peek_token());
            if (!maybe_offset_y)
                return nullptr;
            offset_y = maybe_offset_y;
            tokens.next_token();

            // blur radius (optional)
            if (!tokens.has_next_token())
                break;
            auto maybe_blur_radius = possibly_dynamic_length(tokens.peek_token());
            if (!maybe_blur_radius)
                continue;
            blur_radius = maybe_blur_radius;
            tokens.next_token();

            // spread distance (optional)
            if (!tokens.has_next_token())
                break;
            auto maybe_spread_distance = possibly_dynamic_length(tokens.peek_token());
            if (!maybe_spread_distance)
                continue;
            spread_distance = maybe_spread_distance;
            tokens.next_token();

            continue;
        }

        if (allow_inset_keyword == AllowInsetKeyword::Yes
            && token.is(Token::Type::Ident) && token.token().ident().equals_ignoring_ascii_case("inset"sv)) {
            if (placement.has_value())
                return nullptr;
            placement = ShadowPlacement::Inner;
            tokens.next_token();
            continue;
        }

        if (token.is(Token::Type::Comma))
            break;

        return nullptr;
    }

    // FIXME: If color is absent, default to `currentColor`
    if (!color.has_value())
        color = Color::NamedColor::Black;

    // x/y offsets are required
    if (!offset_x || !offset_y)
        return nullptr;

    // Other lengths default to 0
    if (!blur_radius)
        blur_radius = LengthStyleValue::create(Length::make_px(0));
    if (!spread_distance)
        spread_distance = LengthStyleValue::create(Length::make_px(0));

    // Placement is outer by default
    if (!placement.has_value())
        placement = ShadowPlacement::Outer;

    transaction.commit();
    return ShadowStyleValue::create(color.release_value(), offset_x.release_nonnull(), offset_y.release_nonnull(), blur_radius.release_nonnull(), spread_distance.release_nonnull(), placement.release_value());
}

RefPtr<StyleValue> Parser::parse_content_value(Vector<ComponentValue> const& component_values)
{
    // FIXME: `content` accepts several kinds of function() type, which we don't handle in property_accepts_value() yet.

    auto is_single_value_identifier = [](ValueID identifier) -> bool {
        switch (identifier) {
        case ValueID::None:
        case ValueID::Normal:
            return true;
        default:
            return false;
        }
    };

    if (component_values.size() == 1) {
        if (auto identifier = parse_identifier_value(component_values.first())) {
            if (is_single_value_identifier(identifier->to_identifier()))
                return identifier;
        }
    }

    StyleValueVector content_values;
    StyleValueVector alt_text_values;
    bool in_alt_text = false;

    auto tokens = TokenStream { component_values };
    while (tokens.has_next_token()) {
        auto& next = tokens.peek_token();
        if (next.is_delim('/')) {
            if (in_alt_text || content_values.is_empty())
                return nullptr;
            in_alt_text = true;
            (void)tokens.next_token();
            continue;
        }

        if (auto style_value = parse_css_value_for_property(PropertyID::Content, tokens)) {
            if (is_single_value_identifier(style_value->to_identifier()))
                return nullptr;

            if (in_alt_text) {
                alt_text_values.append(style_value.release_nonnull());
            } else {
                content_values.append(style_value.release_nonnull());
            }
            continue;
        }

        return nullptr;
    }

    if (content_values.is_empty())
        return nullptr;
    if (in_alt_text && alt_text_values.is_empty())
        return nullptr;

    RefPtr<StyleValueList> alt_text;
    if (!alt_text_values.is_empty())
        alt_text = StyleValueList::create(move(alt_text_values), StyleValueList::Separator::Space);

    return ContentStyleValue::create(StyleValueList::create(move(content_values), StyleValueList::Separator::Space), move(alt_text));
}

// https://www.w3.org/TR/css-display-3/#the-display-properties
RefPtr<StyleValue> Parser::parse_display_value(Vector<ComponentValue> const& component_values)
{
    auto parse_inside = [](ValueID identifier) -> Optional<Display::Inside> {
        switch (identifier) {
        case ValueID::Flow:
            return Display::Inside::Flow;
        case ValueID::FlowRoot:
            return Display::Inside::FlowRoot;
        case ValueID::Table:
            return Display::Inside::Table;
        case ValueID::Flex:
            return Display::Inside::Flex;
        case ValueID::Grid:
            return Display::Inside::Grid;
        case ValueID::Ruby:
            return Display::Inside::Ruby;
        default:
            return {};
        }
    };
    auto parse_outside = [](ValueID identifier) -> Optional<Display::Outside> {
        switch (identifier) {
        case ValueID::Block:
            return Display::Outside::Block;
        case ValueID::Inline:
            return Display::Outside::Inline;
        case ValueID::RunIn:
            return Display::Outside::RunIn;
        default:
            return {};
        }
    };

    auto parse_single_component_display = [&](Vector<ComponentValue> const& component_values) -> Optional<Display> {
        if (auto identifier = parse_identifier_value(component_values.first())) {
            switch (identifier->to_identifier()) {

            // display-outside
            case ValueID::Block:
                return Display::from_short(Display::Short::Block);
            case ValueID::Inline:
                return Display::from_short(Display::Short::Inline);
            case ValueID::RunIn:
                return Display::from_short(Display::Short::RunIn);

            // display-inside
            case ValueID::Flow:
                return Display::from_short(Display::Short::Flow);
            case ValueID::FlowRoot:
                return Display::from_short(Display::Short::FlowRoot);
            case ValueID::Table:
                return Display::from_short(Display::Short::Table);
            case ValueID::Flex:
                return Display::from_short(Display::Short::Flex);
            case ValueID::Grid:
                return Display::from_short(Display::Short::Grid);
            case ValueID::Ruby:
                return Display::from_short(Display::Short::Ruby);

            // display-listitem
            case ValueID::ListItem:
                return Display::from_short(Display::Short::ListItem);

            // display-internal
            case ValueID::TableRowGroup:
                return Display { Display::Internal::TableRowGroup };
            case ValueID::TableHeaderGroup:
                return Display { Display::Internal::TableHeaderGroup };
            case ValueID::TableFooterGroup:
                return Display { Display::Internal::TableFooterGroup };
            case ValueID::TableRow:
                return Display { Display::Internal::TableRow };
            case ValueID::TableCell:
                return Display { Display::Internal::TableCell };
            case ValueID::TableColumnGroup:
                return Display { Display::Internal::TableColumnGroup };
            case ValueID::TableColumn:
                return Display { Display::Internal::TableColumn };
            case ValueID::TableCaption:
                return Display { Display::Internal::TableCaption };
            case ValueID::RubyBase:
                return Display { Display::Internal::RubyBase };
            case ValueID::RubyText:
                return Display { Display::Internal::RubyText };
            case ValueID::RubyBaseContainer:
                return Display { Display::Internal::RubyBaseContainer };
            case ValueID::RubyTextContainer:
                return Display { Display::Internal::RubyTextContainer };

            // display-box
            case ValueID::Contents:
                return Display::from_short(Display::Short::Contents);
            case ValueID::None:
                return Display::from_short(Display::Short::None);

            // display-legacy
            case ValueID::InlineBlock:
                return Display::from_short(Display::Short::InlineBlock);
            case ValueID::InlineTable:
                return Display::from_short(Display::Short::InlineTable);
            case ValueID::InlineFlex:
                return Display::from_short(Display::Short::InlineFlex);
            case ValueID::InlineGrid:
                return Display::from_short(Display::Short::InlineGrid);

            default:
                return OptionalNone {};
            }
        }
        return OptionalNone {};
    };

    auto parse_multi_component_display = [&](Vector<ComponentValue> const& component_values) -> Optional<Display> {
        auto list_item = Display::ListItem::No;
        Display::Inside inside = Display::Inside::Flow;
        Display::Outside outside = Display::Outside::Block;

        for (size_t i = 0; i < component_values.size(); ++i) {
            if (auto value = parse_identifier_value(component_values[i])) {
                auto identifier = value->to_identifier();
                if (ValueID::ListItem == identifier) {
                    list_item = Display::ListItem::Yes;
                    continue;
                }
                auto inside_value = parse_inside(identifier);
                if (inside_value.has_value()) {
                    inside = inside_value.value();
                    continue;
                }
                auto outside_value = parse_outside(identifier);
                if (outside_value.has_value()) {
                    outside = outside_value.value();
                }
            }
        }

        // The spec does not allow any other inside values to be combined with list-item
        // <display-outside>? && [ flow | flow-root ]? && list-item
        if (list_item == Display::ListItem::Yes && inside != Display::Inside::Flow && inside != Display::Inside::FlowRoot)
            return OptionalNone {};

        return Display { outside, inside, list_item };
    };

    Optional<Display> display;
    if (component_values.size() == 1)
        display = parse_single_component_display(component_values);
    else
        display = parse_multi_component_display(component_values);

    if (display.has_value())
        return DisplayStyleValue::create(display.value());

    return nullptr;
}

RefPtr<StyleValue> Parser::parse_filter_value_list_value(Vector<ComponentValue> const& component_values)
{
    if (component_values.size() == 1 && component_values.first().is(Token::Type::Ident)) {
        auto ident = parse_identifier_value(component_values.first());
        if (ident && ident->to_identifier() == ValueID::None)
            return ident;
    }

    TokenStream tokens { component_values };

    // FIXME: <url>s are ignored for now
    // <filter-value-list> = [ <filter-function> | <url> ]+

    enum class FilterToken {
        // Color filters:
        Brightness,
        Contrast,
        Grayscale,
        Invert,
        Opacity,
        Saturate,
        Sepia,
        // Special filters:
        Blur,
        DropShadow,
        HueRotate
    };

    auto filter_token_to_operation = [&](auto filter) {
        VERIFY(to_underlying(filter) < to_underlying(FilterToken::Blur));
        return static_cast<Filter::Color::Operation>(filter);
    };

    auto parse_number_percentage = [&](auto& token) -> Optional<NumberPercentage> {
        if (token.is(Token::Type::Percentage))
            return NumberPercentage(Percentage(token.token().percentage()));
        if (token.is(Token::Type::Number))
            return NumberPercentage(Number(Number::Type::Number, token.token().number_value()));
        return {};
    };

    auto parse_filter_function_name = [&](auto name) -> Optional<FilterToken> {
        if (name.equals_ignoring_ascii_case("blur"sv))
            return FilterToken::Blur;
        if (name.equals_ignoring_ascii_case("brightness"sv))
            return FilterToken::Brightness;
        if (name.equals_ignoring_ascii_case("contrast"sv))
            return FilterToken::Contrast;
        if (name.equals_ignoring_ascii_case("drop-shadow"sv))
            return FilterToken::DropShadow;
        if (name.equals_ignoring_ascii_case("grayscale"sv))
            return FilterToken::Grayscale;
        if (name.equals_ignoring_ascii_case("hue-rotate"sv))
            return FilterToken::HueRotate;
        if (name.equals_ignoring_ascii_case("invert"sv))
            return FilterToken::Invert;
        if (name.equals_ignoring_ascii_case("opacity"sv))
            return FilterToken::Opacity;
        if (name.equals_ignoring_ascii_case("saturate"sv))
            return FilterToken::Saturate;
        if (name.equals_ignoring_ascii_case("sepia"sv))
            return FilterToken::Sepia;
        return {};
    };

    auto parse_filter_function = [&](auto filter_token, auto function_values) -> Optional<FilterFunction> {
        TokenStream tokens { function_values };
        tokens.skip_whitespace();

        auto if_no_more_tokens_return = [&](auto filter) -> Optional<FilterFunction> {
            tokens.skip_whitespace();
            if (tokens.has_next_token())
                return {};
            return filter;
        };

        if (filter_token == FilterToken::Blur) {
            // blur( <length>? )
            if (!tokens.has_next_token())
                return Filter::Blur {};
            auto blur_radius = parse_length(tokens.next_token());
            if (!blur_radius.has_value())
                return {};
            return if_no_more_tokens_return(Filter::Blur { *blur_radius });
        } else if (filter_token == FilterToken::DropShadow) {
            if (!tokens.has_next_token())
                return {};
            auto next_token = [&]() -> auto& {
                auto& token = tokens.next_token();
                tokens.skip_whitespace();
                return token;
            };
            // drop-shadow( [ <color>? && <length>{2,3} ] )
            // Note: The following code is a little awkward to allow the color to be before or after the lengths.
            auto& first_param = next_token();
            Optional<Length> maybe_radius = {};
            auto maybe_color = parse_color(first_param);
            auto x_offset = parse_length(maybe_color.has_value() ? next_token() : first_param);
            if (!x_offset.has_value() || !tokens.has_next_token()) {
                return {};
            }
            auto y_offset = parse_length(next_token());
            if (!y_offset.has_value()) {
                return {};
            }
            if (tokens.has_next_token()) {
                auto& token = next_token();
                maybe_radius = parse_length(token);
                if (!maybe_color.has_value() && (!maybe_radius.has_value() || tokens.has_next_token())) {
                    maybe_color = parse_color(!maybe_radius.has_value() ? token : next_token());
                    if (!maybe_color.has_value()) {
                        return {};
                    }
                } else if (!maybe_radius.has_value()) {
                    return {};
                }
            }
            return if_no_more_tokens_return(Filter::DropShadow { *x_offset, *y_offset, maybe_radius, maybe_color });
        } else if (filter_token == FilterToken::HueRotate) {
            // hue-rotate( [ <angle> | <zero> ]? )
            if (!tokens.has_next_token())
                return Filter::HueRotate {};
            auto& token = tokens.next_token();
            if (token.is(Token::Type::Number)) {
                // hue-rotate(0)
                auto number = token.token().number();
                if (number.is_integer() && number.integer_value() == 0)
                    return if_no_more_tokens_return(Filter::HueRotate { Filter::HueRotate::Zero {} });
                return {};
            }
            if (!token.is(Token::Type::Dimension))
                return {};
            auto angle_value = token.token().dimension_value();
            auto angle_unit_name = token.token().dimension_unit();
            auto angle_unit = Angle::unit_from_name(angle_unit_name);
            if (!angle_unit.has_value())
                return {};
            Angle angle { angle_value, angle_unit.release_value() };
            return if_no_more_tokens_return(Filter::HueRotate { angle });
        } else {
            // Simple filters:
            // brightness( <number-percentage>? )
            // contrast( <number-percentage>? )
            // grayscale( <number-percentage>? )
            // invert( <number-percentage>? )
            // opacity( <number-percentage>? )
            // sepia( <number-percentage>? )
            // saturate( <number-percentage>? )
            if (!tokens.has_next_token())
                return Filter::Color { filter_token_to_operation(filter_token) };
            auto amount = parse_number_percentage(tokens.next_token());
            if (!amount.has_value())
                return {};
            return if_no_more_tokens_return(Filter::Color { filter_token_to_operation(filter_token), *amount });
        }
    };

    Vector<FilterFunction> filter_value_list {};

    while (tokens.has_next_token()) {
        tokens.skip_whitespace();
        if (!tokens.has_next_token())
            break;
        auto& token = tokens.next_token();
        if (!token.is_function())
            return nullptr;
        auto filter_token = parse_filter_function_name(token.function().name());
        if (!filter_token.has_value())
            return nullptr;
        auto filter_function = parse_filter_function(*filter_token, token.function().values());
        if (!filter_function.has_value())
            return nullptr;
        filter_value_list.append(*filter_function);
    }

    if (filter_value_list.is_empty())
        return nullptr;

    return FilterValueListStyleValue::create(move(filter_value_list));
}

RefPtr<StyleValue> Parser::parse_flex_value(Vector<ComponentValue> const& component_values)
{
    auto tokens = TokenStream { component_values };

    if (component_values.size() == 1) {
        // One-value syntax: <flex-grow> | <flex-basis> | none
        auto properties = Array { PropertyID::FlexGrow, PropertyID::FlexBasis, PropertyID::Flex };
        auto property_and_value = parse_css_value_for_properties(properties, tokens);
        if (!property_and_value.has_value())
            return nullptr;

        auto& value = property_and_value->style_value;
        switch (property_and_value->property) {
        case PropertyID::FlexGrow: {
            // NOTE: The spec says that flex-basis should be 0 here, but other engines currently use 0%.
            // https://github.com/w3c/csswg-drafts/issues/5742
            auto flex_basis = PercentageStyleValue::create(Percentage(0));
            auto one = NumberStyleValue::create(1);
            return FlexStyleValue::create(*value, one, flex_basis);
        }
        case PropertyID::FlexBasis: {
            auto one = NumberStyleValue::create(1);
            return FlexStyleValue::create(one, one, *value);
        }
        case PropertyID::Flex: {
            if (value->is_identifier() && value->to_identifier() == ValueID::None) {
                auto zero = NumberStyleValue::create(0);
                return FlexStyleValue::create(zero, zero, IdentifierStyleValue::create(ValueID::Auto));
            }
            break;
        }
        default:
            VERIFY_NOT_REACHED();
        }

        return nullptr;
    }

    RefPtr<StyleValue> flex_grow;
    RefPtr<StyleValue> flex_shrink;
    RefPtr<StyleValue> flex_basis;

    // NOTE: FlexGrow has to be before FlexBasis. `0` is a valid FlexBasis, but only
    //       if FlexGrow (along with optional FlexShrink) have already been specified.
    auto remaining_longhands = Vector { PropertyID::FlexGrow, PropertyID::FlexBasis };

    while (tokens.has_next_token()) {
        auto property_and_value = parse_css_value_for_properties(remaining_longhands, tokens);
        if (!property_and_value.has_value())
            return nullptr;
        auto& value = property_and_value->style_value;
        remove_property(remaining_longhands, property_and_value->property);

        switch (property_and_value->property) {
        case PropertyID::FlexGrow: {
            VERIFY(!flex_grow);
            flex_grow = value.release_nonnull();

            // Flex-shrink may optionally follow directly after.
            auto maybe_flex_shrink = parse_css_value_for_property(PropertyID::FlexShrink, tokens);
            if (maybe_flex_shrink)
                flex_shrink = maybe_flex_shrink.release_nonnull();
            continue;
        }
        case PropertyID::FlexBasis: {
            VERIFY(!flex_basis);
            flex_basis = value.release_nonnull();
            continue;
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }

    if (!flex_grow)
        flex_grow = property_initial_value(m_context.realm(), PropertyID::FlexGrow);
    if (!flex_shrink)
        flex_shrink = property_initial_value(m_context.realm(), PropertyID::FlexShrink);
    if (!flex_basis) {
        // NOTE: The spec says that flex-basis should be 0 here, but other engines currently use 0%.
        // https://github.com/w3c/csswg-drafts/issues/5742
        flex_basis = PercentageStyleValue::create(Percentage(0));
    }

    return FlexStyleValue::create(flex_grow.release_nonnull(), flex_shrink.release_nonnull(), flex_basis.release_nonnull());
}

RefPtr<StyleValue> Parser::parse_flex_flow_value(Vector<ComponentValue> const& component_values)
{
    if (component_values.size() > 2)
        return nullptr;

    RefPtr<StyleValue> flex_direction;
    RefPtr<StyleValue> flex_wrap;

    auto remaining_longhands = Vector { PropertyID::FlexDirection, PropertyID::FlexWrap };
    auto tokens = TokenStream { component_values };
    while (tokens.has_next_token()) {
        auto property_and_value = parse_css_value_for_properties(remaining_longhands, tokens);
        if (!property_and_value.has_value())
            return nullptr;
        auto& value = property_and_value->style_value;
        remove_property(remaining_longhands, property_and_value->property);

        switch (property_and_value->property) {
        case PropertyID::FlexDirection:
            VERIFY(!flex_direction);
            flex_direction = value.release_nonnull();
            continue;
        case PropertyID::FlexWrap:
            VERIFY(!flex_wrap);
            flex_wrap = value.release_nonnull();
            continue;
        default:
            VERIFY_NOT_REACHED();
        }
    }

    if (!flex_direction)
        flex_direction = property_initial_value(m_context.realm(), PropertyID::FlexDirection);
    if (!flex_wrap)
        flex_wrap = property_initial_value(m_context.realm(), PropertyID::FlexWrap);

    return FlexFlowStyleValue::create(flex_direction.release_nonnull(), flex_wrap.release_nonnull());
}

static bool is_generic_font_family(ValueID identifier)
{
    switch (identifier) {
    case ValueID::Cursive:
    case ValueID::Fantasy:
    case ValueID::Monospace:
    case ValueID::Serif:
    case ValueID::SansSerif:
    case ValueID::UiMonospace:
    case ValueID::UiRounded:
    case ValueID::UiSerif:
    case ValueID::UiSansSerif:
        return true;
    default:
        return false;
    }
}

RefPtr<StyleValue> Parser::parse_font_value(Vector<ComponentValue> const& component_values)
{
    RefPtr<StyleValue> font_stretch;
    RefPtr<StyleValue> font_style;
    RefPtr<StyleValue> font_weight;
    RefPtr<StyleValue> font_size;
    RefPtr<StyleValue> line_height;
    RefPtr<StyleValue> font_families;
    RefPtr<StyleValue> font_variant;

    // FIXME: Handle system fonts. (caption, icon, menu, message-box, small-caption, status-bar)

    // Several sub-properties can be "normal", and appear in any order: style, variant, weight, stretch
    // So, we have to handle that separately.
    int normal_count = 0;

    auto tokens = TokenStream { component_values };
    auto remaining_longhands = Vector { PropertyID::FontSize, PropertyID::FontStretch, PropertyID::FontStyle, PropertyID::FontVariant, PropertyID::FontWeight };

    while (tokens.has_next_token()) {
        auto& peek_token = tokens.peek_token();
        if (peek_token.is(Token::Type::Ident) && value_id_from_string(peek_token.token().ident()) == ValueID::Normal) {
            normal_count++;
            (void)tokens.next_token();
            continue;
        }

        auto property_and_value = parse_css_value_for_properties(remaining_longhands, tokens);
        if (!property_and_value.has_value())
            return nullptr;
        auto& value = property_and_value->style_value;
        remove_property(remaining_longhands, property_and_value->property);

        switch (property_and_value->property) {
        case PropertyID::FontSize: {
            VERIFY(!font_size);
            font_size = value.release_nonnull();

            // Consume `/ line-height` if present
            if (tokens.peek_token().is_delim('/')) {
                (void)tokens.next_token();
                auto maybe_line_height = parse_css_value_for_property(PropertyID::LineHeight, tokens);
                if (!maybe_line_height)
                    return nullptr;
                line_height = maybe_line_height.release_nonnull();
            }

            // Consume font-families
            auto maybe_font_families = parse_font_family_value(tokens);
            // font-family comes last, so we must not have any tokens left over.
            if (!maybe_font_families || tokens.has_next_token())
                return nullptr;
            font_families = maybe_font_families.release_nonnull();
            continue;
        }
        case PropertyID::FontStretch: {
            VERIFY(!font_stretch);
            font_stretch = value.release_nonnull();
            continue;
        }
        case PropertyID::FontStyle: {
            // FIXME: Handle angle parameter to `oblique`: https://www.w3.org/TR/css-fonts-4/#font-style-prop
            VERIFY(!font_style);
            font_style = value.release_nonnull();
            continue;
        }
        case PropertyID::FontVariant: {
            VERIFY(!font_variant);
            font_variant = value.release_nonnull();
            continue;
        }
        case PropertyID::FontWeight: {
            VERIFY(!font_weight);
            font_weight = value.release_nonnull();
            continue;
        }
        default:
            VERIFY_NOT_REACHED();
        }

        return nullptr;
    }

    // Since normal is the default value for all the properties that can have it, we don't have to actually
    // set anything to normal here. It'll be set when we create the FontStyleValue below.
    // We just need to make sure we were not given more normals than will fit.
    int unset_value_count = (font_style ? 0 : 1) + (font_weight ? 0 : 1) + (font_variant ? 0 : 1) + (font_stretch ? 0 : 1);
    if (unset_value_count < normal_count)
        return nullptr;

    if (!font_size || !font_families)
        return nullptr;

    if (!font_stretch)
        font_stretch = property_initial_value(m_context.realm(), PropertyID::FontStretch);
    if (!font_style)
        font_style = property_initial_value(m_context.realm(), PropertyID::FontStyle);
    if (!font_weight)
        font_weight = property_initial_value(m_context.realm(), PropertyID::FontWeight);
    if (!line_height)
        line_height = property_initial_value(m_context.realm(), PropertyID::LineHeight);

    return FontStyleValue::create(font_stretch.release_nonnull(), font_style.release_nonnull(), font_weight.release_nonnull(), font_size.release_nonnull(), line_height.release_nonnull(), font_families.release_nonnull());
}

RefPtr<StyleValue> Parser::parse_font_family_value(TokenStream<ComponentValue>& tokens)
{
    auto next_is_comma_or_eof = [&]() -> bool {
        return !tokens.has_next_token() || tokens.peek_token().is(Token::Type::Comma);
    };

    // Note: Font-family names can either be a quoted string, or a keyword, or a series of custom-idents.
    // eg, these are equivalent:
    //     font-family: my cool     font\!, serif;
    //     font-family: "my cool font!", serif;
    StyleValueVector font_families;
    Vector<DeprecatedString> current_name_parts;
    while (tokens.has_next_token()) {
        auto const& peek = tokens.peek_token();

        if (peek.is(Token::Type::String)) {
            // `font-family: my cool "font";` is invalid.
            if (!current_name_parts.is_empty())
                return nullptr;
            (void)tokens.next_token(); // String
            if (!next_is_comma_or_eof())
                return nullptr;
            font_families.append(StringStyleValue::create(MUST(String::from_utf8(peek.token().string()))));
            (void)tokens.next_token(); // Comma
            continue;
        }

        if (peek.is(Token::Type::Ident)) {
            // If this is a valid identifier, it's NOT a custom-ident and can't be part of a larger name.

            // CSS-wide keywords are not allowed
            if (auto builtin = parse_builtin_value(peek))
                return nullptr;

            auto maybe_ident = value_id_from_string(peek.token().ident());
            // Can't have a generic-font-name as a token in an unquoted font name.
            if (maybe_ident.has_value() && is_generic_font_family(maybe_ident.value())) {
                if (!current_name_parts.is_empty())
                    return nullptr;
                (void)tokens.next_token(); // Ident
                if (!next_is_comma_or_eof())
                    return nullptr;
                font_families.append(IdentifierStyleValue::create(maybe_ident.value()));
                (void)tokens.next_token(); // Comma
                continue;
            }
            current_name_parts.append(tokens.next_token().token().ident());
            continue;
        }

        if (peek.is(Token::Type::Comma)) {
            if (current_name_parts.is_empty())
                return nullptr;
            (void)tokens.next_token(); // Comma
            font_families.append(StringStyleValue::create(MUST(String::join(' ', current_name_parts))));
            current_name_parts.clear();
            // Can't have a trailing comma
            if (!tokens.has_next_token())
                return nullptr;
            continue;
        }

        return nullptr;
    }

    if (!current_name_parts.is_empty()) {
        font_families.append(StringStyleValue::create(MUST(String::join(' ', current_name_parts))));
        current_name_parts.clear();
    }

    if (font_families.is_empty())
        return nullptr;
    return StyleValueList::create(move(font_families), StyleValueList::Separator::Comma);
}

CSSRule* Parser::parse_font_face_rule(TokenStream<ComponentValue>& tokens)
{
    auto declarations_and_at_rules = parse_a_list_of_declarations(tokens);

    Optional<FlyString> font_family;
    Vector<FontFace::Source> src;
    Vector<UnicodeRange> unicode_range;
    Optional<int> weight;
    Optional<int> slope;

    for (auto& declaration_or_at_rule : declarations_and_at_rules) {
        if (declaration_or_at_rule.is_at_rule()) {
            dbgln_if(CSS_PARSER_DEBUG, "CSSParser: CSS at-rules are not allowed in @font-face; discarding.");
            continue;
        }

        auto const& declaration = declaration_or_at_rule.declaration();
        if (declaration.name().equals_ignoring_ascii_case("font-weight"sv)) {
            TokenStream token_stream { declaration.values() };
            if (auto value = parse_css_value(CSS::PropertyID::FontWeight, token_stream); !value.is_error()) {
                weight = value.value()->to_font_weight();
            }
            continue;
        }
        if (declaration.name().equals_ignoring_ascii_case("font-style"sv)) {
            TokenStream token_stream { declaration.values() };
            if (auto value = parse_css_value(CSS::PropertyID::FontStyle, token_stream); !value.is_error()) {
                slope = value.value()->to_font_slope();
            }
            continue;
        }
        if (declaration.name().equals_ignoring_ascii_case("font-family"sv)) {
            // FIXME: This is very similar to, but different from, the logic in parse_font_family_value().
            //        Ideally they could share code.
            Vector<DeprecatedString> font_family_parts;
            bool had_syntax_error = false;
            for (size_t i = 0; i < declaration.values().size(); ++i) {
                auto const& part = declaration.values()[i];
                if (part.is(Token::Type::Whitespace))
                    continue;
                if (part.is(Token::Type::String)) {
                    if (!font_family_parts.is_empty()) {
                        dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @font-face font-family format invalid; discarding.");
                        had_syntax_error = true;
                        break;
                    }
                    font_family_parts.append(part.token().string());
                    continue;
                }
                if (part.is(Token::Type::Ident)) {
                    if (is_builtin(part.token().ident())) {
                        dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @font-face font-family format invalid; discarding.");
                        had_syntax_error = true;
                        break;
                    }
                    auto value_id = value_id_from_string(part.token().ident());
                    if (value_id.has_value() && is_generic_font_family(value_id.value())) {
                        dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @font-face font-family format invalid; discarding.");
                        had_syntax_error = true;
                        break;
                    }
                    font_family_parts.append(part.token().ident());
                    continue;
                }

                dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @font-face font-family format invalid; discarding.");
                had_syntax_error = true;
                break;
            }
            if (had_syntax_error || font_family_parts.is_empty())
                continue;

            font_family = String::join(' ', font_family_parts).release_value_but_fixme_should_propagate_errors();
            continue;
        }
        if (declaration.name().equals_ignoring_ascii_case("src"sv)) {
            TokenStream token_stream { declaration.values() };
            Vector<FontFace::Source> supported_sources = parse_font_face_src(token_stream);
            if (!supported_sources.is_empty())
                src = move(supported_sources);
            continue;
        }
        if (declaration.name().equals_ignoring_ascii_case("unicode-range"sv)) {
            Vector<UnicodeRange> unicode_ranges;
            bool unicode_range_invalid = false;
            TokenStream all_tokens { declaration.values() };
            auto range_token_lists = parse_a_comma_separated_list_of_component_values(all_tokens);
            for (auto& range_tokens : range_token_lists) {
                TokenStream range_token_stream { range_tokens };
                auto maybe_unicode_range = parse_unicode_range(range_token_stream);
                if (!maybe_unicode_range.has_value()) {
                    dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @font-face unicode-range format invalid; discarding.");
                    unicode_range_invalid = true;
                    break;
                }
                unicode_ranges.append(maybe_unicode_range.release_value());
            }

            if (unicode_range_invalid || unicode_ranges.is_empty())
                continue;

            unicode_range = move(unicode_ranges);
            continue;
        }

        dbgln_if(CSS_PARSER_DEBUG, "CSSParser: Unrecognized descriptor '{}' in @font-face; discarding.", declaration.name());
    }

    if (!font_family.has_value()) {
        dbgln_if(CSS_PARSER_DEBUG, "CSSParser: Failed to parse @font-face: no font-family!");
        return {};
    }

    if (unicode_range.is_empty()) {
        unicode_range.empend(0x0u, 0x10FFFFu);
    }

    return CSSFontFaceRule::create(m_context.realm(), FontFace { font_family.release_value(), weight, slope, move(src), move(unicode_range) });
}

Vector<FontFace::Source> Parser::parse_font_face_src(TokenStream<ComponentValue>& component_values)
{
    // FIXME: Get this information from the system somehow?
    // Format-name table: https://www.w3.org/TR/css-fonts-4/#font-format-definitions
    auto font_format_is_supported = [](StringView name) {
        // The spec requires us to treat opentype and truetype as synonymous.
        if (name.is_one_of_ignoring_ascii_case("opentype"sv, "truetype"sv, "woff"sv, "woff2"sv))
            return true;
        return false;
    };

    Vector<FontFace::Source> supported_sources;

    auto list_of_source_token_lists = parse_a_comma_separated_list_of_component_values(component_values);
    for (auto const& source_token_list : list_of_source_token_lists) {
        TokenStream source_tokens { source_token_list };
        source_tokens.skip_whitespace();
        auto const& first = source_tokens.next_token();

        // <url> [ format(<font-format>)]?
        // FIXME: Implement optional tech() function from CSS-Fonts-4.
        if (auto maybe_url = parse_url_function(first); maybe_url.has_value()) {
            auto url = maybe_url.release_value();
            if (!url.is_valid()) {
                continue;
            }

            Optional<FlyString> format;

            source_tokens.skip_whitespace();
            if (!source_tokens.has_next_token()) {
                supported_sources.empend(move(url), format);
                continue;
            }

            auto maybe_function = source_tokens.next_token();
            if (!maybe_function.is_function()) {
                dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @font-face src invalid (token after `url()` that isn't a function: {}); discarding.", maybe_function.to_debug_string());
                return {};
            }

            auto const& function = maybe_function.function();
            if (function.name().equals_ignoring_ascii_case("format"sv)) {
                TokenStream format_tokens { function.values() };
                format_tokens.skip_whitespace();
                auto const& format_name_token = format_tokens.next_token();
                StringView format_name;
                if (format_name_token.is(Token::Type::Ident)) {
                    format_name = format_name_token.token().ident();
                } else if (format_name_token.is(Token::Type::String)) {
                    format_name = format_name_token.token().string();
                } else {
                    dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @font-face src invalid (`format()` parameter not an ident or string; is: {}); discarding.", format_name_token.to_debug_string());
                    return {};
                }

                if (!font_format_is_supported(format_name)) {
                    dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @font-face src format({}) not supported; skipping.", format_name);
                    continue;
                }

                format = FlyString::from_utf8(format_name).release_value_but_fixme_should_propagate_errors();
            } else {
                dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @font-face src invalid (unrecognized function token `{}`); discarding.", function.name());
                return {};
            }

            source_tokens.skip_whitespace();
            if (source_tokens.has_next_token()) {
                dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @font-face src invalid (extra token `{}`); discarding.", source_tokens.peek_token().to_debug_string());
                return {};
            }

            supported_sources.empend(move(url), format);
            continue;
        }

        if (first.is_function() && first.function().name().equals_ignoring_ascii_case("local"sv)) {
            if (first.function().values().is_empty()) {
                continue;
            }
            supported_sources.empend(first.function().values().first().to_string(), Optional<FlyString> {});
            continue;
        }

        dbgln_if(CSS_PARSER_DEBUG, "CSSParser: @font-face src invalid (failed to parse url from: {}); discarding.", first.to_debug_string());
        return {};
    }

    return supported_sources;
}

RefPtr<StyleValue> Parser::parse_list_style_value(Vector<ComponentValue> const& component_values)
{
    if (component_values.size() > 3)
        return nullptr;

    RefPtr<StyleValue> list_position;
    RefPtr<StyleValue> list_image;
    RefPtr<StyleValue> list_type;
    int found_nones = 0;

    Vector<PropertyID> remaining_longhands { PropertyID::ListStyleImage, PropertyID::ListStylePosition, PropertyID::ListStyleType };

    auto tokens = TokenStream { component_values };
    while (tokens.has_next_token()) {
        if (auto peek = tokens.peek_token(); peek.is(Token::Type::Ident) && peek.token().ident().equals_ignoring_ascii_case("none"sv)) {
            (void)tokens.next_token();
            found_nones++;
            continue;
        }

        auto property_and_value = parse_css_value_for_properties(remaining_longhands, tokens);
        if (!property_and_value.has_value())
            return nullptr;
        auto& value = property_and_value->style_value;
        remove_property(remaining_longhands, property_and_value->property);

        switch (property_and_value->property) {
        case PropertyID::ListStylePosition: {
            VERIFY(!list_position);
            list_position = value.release_nonnull();
            continue;
        }
        case PropertyID::ListStyleImage: {
            VERIFY(!list_image);
            list_image = value.release_nonnull();
            continue;
        }
        case PropertyID::ListStyleType: {
            VERIFY(!list_type);
            list_type = value.release_nonnull();
            continue;
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }

    if (found_nones > 2)
        return nullptr;

    if (found_nones == 2) {
        if (list_image || list_type)
            return nullptr;
        auto none = IdentifierStyleValue::create(ValueID::None);
        list_image = none;
        list_type = none;

    } else if (found_nones == 1) {
        if (list_image && list_type)
            return nullptr;
        auto none = IdentifierStyleValue::create(ValueID::None);
        if (!list_image)
            list_image = none;
        if (!list_type)
            list_type = none;
    }

    if (!list_position)
        list_position = property_initial_value(m_context.realm(), PropertyID::ListStylePosition);
    if (!list_image)
        list_image = property_initial_value(m_context.realm(), PropertyID::ListStyleImage);
    if (!list_type)
        list_type = property_initial_value(m_context.realm(), PropertyID::ListStyleType);

    return ListStyleStyleValue::create(list_position.release_nonnull(), list_image.release_nonnull(), list_type.release_nonnull());
}

RefPtr<StyleValue> Parser::parse_overflow_value(Vector<ComponentValue> const& component_values)
{
    auto tokens = TokenStream { component_values };
    if (component_values.size() == 1) {
        auto maybe_value = parse_css_value_for_property(PropertyID::Overflow, tokens);
        if (!maybe_value)
            return nullptr;
        return OverflowStyleValue::create(*maybe_value, *maybe_value);
    }

    if (component_values.size() == 2) {
        auto maybe_x_value = parse_css_value_for_property(PropertyID::OverflowX, tokens);
        auto maybe_y_value = parse_css_value_for_property(PropertyID::OverflowY, tokens);
        if (!maybe_x_value || !maybe_y_value)
            return nullptr;
        return OverflowStyleValue::create(maybe_x_value.release_nonnull(), maybe_y_value.release_nonnull());
    }

    return nullptr;
}

RefPtr<StyleValue> Parser::parse_place_content_value(Vector<ComponentValue> const& component_values)
{
    if (component_values.size() > 2)
        return nullptr;

    auto tokens = TokenStream { component_values };
    auto maybe_align_content_value = parse_css_value_for_property(PropertyID::AlignContent, tokens);
    if (!maybe_align_content_value)
        return nullptr;

    if (component_values.size() == 1) {
        if (!property_accepts_identifier(PropertyID::JustifyContent, maybe_align_content_value->to_identifier()))
            return nullptr;
        return PlaceContentStyleValue::create(*maybe_align_content_value, *maybe_align_content_value);
    }

    auto maybe_justify_content_value = parse_css_value_for_property(PropertyID::JustifyContent, tokens);
    if (!maybe_justify_content_value)
        return nullptr;
    return PlaceContentStyleValue::create(maybe_align_content_value.release_nonnull(), maybe_justify_content_value.release_nonnull());
}

RefPtr<StyleValue> Parser::parse_place_items_value(Vector<ComponentValue> const& component_values)
{
    auto tokens = TokenStream { component_values };
    auto maybe_align_items_value = parse_css_value_for_property(PropertyID::AlignItems, tokens);
    if (!maybe_align_items_value)
        return nullptr;

    if (component_values.size() == 1) {
        if (!property_accepts_identifier(PropertyID::JustifyItems, maybe_align_items_value->to_identifier()))
            return nullptr;
        return PlaceItemsStyleValue::create(*maybe_align_items_value, *maybe_align_items_value);
    }

    auto maybe_justify_items_value = parse_css_value_for_property(PropertyID::JustifyItems, tokens);
    if (!maybe_justify_items_value)
        return nullptr;
    return PlaceItemsStyleValue::create(*maybe_align_items_value, *maybe_justify_items_value);
}

RefPtr<StyleValue> Parser::parse_place_self_value(Vector<ComponentValue> const& component_values)
{
    auto tokens = TokenStream { component_values };
    auto maybe_align_self_value = parse_css_value_for_property(PropertyID::AlignSelf, tokens);
    if (!maybe_align_self_value)
        return nullptr;

    if (component_values.size() == 1) {
        if (!property_accepts_identifier(PropertyID::JustifySelf, maybe_align_self_value->to_identifier()))
            return nullptr;
        return PlaceSelfStyleValue::create(*maybe_align_self_value, *maybe_align_self_value);
    }

    auto maybe_justify_self_value = parse_css_value_for_property(PropertyID::JustifySelf, tokens);
    if (!maybe_justify_self_value)
        return nullptr;
    return PlaceItemsStyleValue::create(*maybe_align_self_value, *maybe_justify_self_value);
}

RefPtr<StyleValue> Parser::parse_text_decoration_value(Vector<ComponentValue> const& component_values)
{
    RefPtr<StyleValue> decoration_line;
    RefPtr<StyleValue> decoration_thickness;
    RefPtr<StyleValue> decoration_style;
    RefPtr<StyleValue> decoration_color;

    auto remaining_longhands = Vector { PropertyID::TextDecorationColor, PropertyID::TextDecorationLine, PropertyID::TextDecorationStyle, PropertyID::TextDecorationThickness };

    auto tokens = TokenStream { component_values };
    while (tokens.has_next_token()) {
        auto property_and_value = parse_css_value_for_properties(remaining_longhands, tokens);
        if (!property_and_value.has_value())
            return nullptr;
        auto& value = property_and_value->style_value;
        remove_property(remaining_longhands, property_and_value->property);

        switch (property_and_value->property) {
        case PropertyID::TextDecorationColor: {
            VERIFY(!decoration_color);
            decoration_color = value.release_nonnull();
            continue;
        }
        case PropertyID::TextDecorationLine: {
            VERIFY(!decoration_line);
            tokens.reconsume_current_input_token();
            auto parsed_decoration_line = parse_text_decoration_line_value(tokens);
            if (!parsed_decoration_line)
                return nullptr;
            decoration_line = parsed_decoration_line.release_nonnull();
            continue;
        }
        case PropertyID::TextDecorationThickness: {
            VERIFY(!decoration_thickness);
            decoration_thickness = value.release_nonnull();
            continue;
        }
        case PropertyID::TextDecorationStyle: {
            VERIFY(!decoration_style);
            decoration_style = value.release_nonnull();
            continue;
        }
        default:
            VERIFY_NOT_REACHED();
        }
    }

    if (!decoration_line)
        decoration_line = property_initial_value(m_context.realm(), PropertyID::TextDecorationLine);
    if (!decoration_thickness)
        decoration_thickness = property_initial_value(m_context.realm(), PropertyID::TextDecorationThickness);
    if (!decoration_style)
        decoration_style = property_initial_value(m_context.realm(), PropertyID::TextDecorationStyle);
    if (!decoration_color)
        decoration_color = property_initial_value(m_context.realm(), PropertyID::TextDecorationColor);

    return TextDecorationStyleValue::create(decoration_line.release_nonnull(), decoration_thickness.release_nonnull(), decoration_style.release_nonnull(), decoration_color.release_nonnull());
}

RefPtr<StyleValue> Parser::parse_text_decoration_line_value(TokenStream<ComponentValue>& tokens)
{
    StyleValueVector style_values;

    while (tokens.has_next_token()) {
        auto maybe_value = parse_css_value_for_property(PropertyID::TextDecorationLine, tokens);
        if (!maybe_value)
            break;
        auto value = maybe_value.release_nonnull();

        if (auto maybe_line = value_id_to_text_decoration_line(value->to_identifier()); maybe_line.has_value()) {
            if (maybe_line == TextDecorationLine::None) {
                if (!style_values.is_empty())
                    break;
                return value;
            }
            if (style_values.contains_slow(value))
                break;
            style_values.append(move(value));
            continue;
        }

        break;
    }

    if (style_values.is_empty())
        return nullptr;
    return StyleValueList::create(move(style_values), StyleValueList::Separator::Space);
}

RefPtr<StyleValue> Parser::parse_easing_value(TokenStream<ComponentValue>& tokens)
{
    auto transaction = tokens.begin_transaction();

    tokens.skip_whitespace();

    auto const& part = tokens.next_token();

    StringView name;
    Optional<Vector<ComponentValue> const&> arguments;
    if (part.is(Token::Type::Ident)) {
        name = part.token().ident();
    } else if (part.is_function()) {
        name = part.function().name();
        arguments = part.function().values();
    } else {
        return nullptr;
    }

    auto maybe_function = easing_function_from_string(name);
    if (!maybe_function.has_value())
        return nullptr;

    auto function = maybe_function.release_value();
    auto function_metadata = easing_function_metadata(function);

    if (function_metadata.parameters.is_empty() && arguments.has_value()) {
        dbgln_if(CSS_PARSER_DEBUG, "Too many arguments to {}. max: 0", name);
        return nullptr;
    }

    StyleValueVector values;
    size_t argument_index = 0;
    if (arguments.has_value()) {
        auto argument_tokens = TokenStream { *arguments };
        auto arguments_values = parse_a_comma_separated_list_of_component_values(argument_tokens);
        if (arguments_values.size() > function_metadata.parameters.size()) {
            dbgln_if(CSS_PARSER_DEBUG, "Too many arguments to {}. max: {}", name, function_metadata.parameters.size());
            return nullptr;
        }
        for (auto& argument_values : arguments_values) {
            // Prune any whitespace before and after the actual argument values.
            argument_values.remove_all_matching([](auto& value) { return value.is(Token::Type::Whitespace); });

            if (argument_values.size() != 1) {
                dbgln_if(CSS_PARSER_DEBUG, "Too many values in argument to {}. max: 1", name);
                return nullptr;
            }

            auto& value = argument_values[0];
            switch (function_metadata.parameters[argument_index].type) {
            case EasingFunctionParameterType::Number: {
                if (value.is(Token::Type::Number))
                    values.append(NumberStyleValue::create(value.token().number().value()));
                else
                    return nullptr;
                break;
            }
            case EasingFunctionParameterType::NumberZeroToOne: {
                if (value.is(Token::Type::Number) && value.token().number_value() >= 0 && value.token().number_value() <= 1)
                    values.append(NumberStyleValue::create(value.token().number().value()));
                else
                    return nullptr;
                break;
            }
            case EasingFunctionParameterType::Integer: {
                if (value.is(Token::Type::Number) && value.token().number().is_integer())
                    values.append(IntegerStyleValue::create(value.token().number().integer_value()));
                else
                    return nullptr;
                break;
            }
            case EasingFunctionParameterType::StepPosition: {
                if (!value.is(Token::Type::Ident))
                    return nullptr;
                auto ident = parse_identifier_value(value);
                if (!ident)
                    return nullptr;
                switch (ident->to_identifier()) {
                case ValueID::JumpStart:
                case ValueID::JumpEnd:
                case ValueID::JumpNone:
                case ValueID::Start:
                case ValueID::End:
                    values.append(*ident);
                    break;
                default:
                    return nullptr;
                }
            }
            }

            ++argument_index;
        }
    }

    if (argument_index < function_metadata.parameters.size() && !function_metadata.parameters[argument_index].is_optional) {
        dbgln_if(CSS_PARSER_DEBUG, "Required parameter at position {} is missing", argument_index);
        return nullptr;
    }

    transaction.commit();
    return EasingStyleValue::create(function, move(values));
}

RefPtr<StyleValue> Parser::parse_transform_value(Vector<ComponentValue> const& component_values)
{
    StyleValueVector transformations;
    auto tokens = TokenStream { component_values };
    tokens.skip_whitespace();

    while (tokens.has_next_token()) {
        tokens.skip_whitespace();
        auto const& part = tokens.next_token();

        if (part.is(Token::Type::Ident) && part.token().ident().equals_ignoring_ascii_case("none"sv)) {
            if (!transformations.is_empty())
                return nullptr;
            tokens.skip_whitespace();
            if (tokens.has_next_token())
                return nullptr;
            return IdentifierStyleValue::create(ValueID::None);
        }

        if (!part.is_function())
            return nullptr;
        auto maybe_function = transform_function_from_string(part.function().name());
        if (!maybe_function.has_value())
            return nullptr;
        auto function = maybe_function.release_value();
        auto function_metadata = transform_function_metadata(function);

        StyleValueVector values;
        auto argument_tokens = TokenStream { part.function().values() };
        argument_tokens.skip_whitespace();
        size_t argument_index = 0;
        while (argument_tokens.has_next_token()) {
            if (argument_index == function_metadata.parameters.size()) {
                dbgln_if(CSS_PARSER_DEBUG, "Too many arguments to {}. max: {}", part.function().name(), function_metadata.parameters.size());
                return nullptr;
            }

            auto const& value = argument_tokens.next_token();
            RefPtr<CalculatedStyleValue> maybe_calc_value = parse_calculated_value(value);

            switch (function_metadata.parameters[argument_index].type) {
            case TransformFunctionParameterType::Angle: {
                // These are `<angle> | <zero>` in the spec, so we have to check for both kinds.
                if (maybe_calc_value && maybe_calc_value->resolves_to_angle()) {
                    values.append(maybe_calc_value.release_nonnull());
                } else if (value.is(Token::Type::Number) && value.token().number_value() == 0) {
                    values.append(AngleStyleValue::create(Angle::make_degrees(0)));
                } else {
                    auto dimension_value = parse_dimension_value(value);
                    if (!dimension_value || !dimension_value->is_angle())
                        return nullptr;
                    values.append(dimension_value.release_nonnull());
                }
                break;
            }
            case TransformFunctionParameterType::Length: {
                if (maybe_calc_value && maybe_calc_value->resolves_to_length()) {
                    values.append(maybe_calc_value.release_nonnull());
                } else {
                    auto dimension_value = parse_dimension_value(value);
                    if (!dimension_value)
                        return nullptr;

                    if (dimension_value->is_length())
                        values.append(dimension_value.release_nonnull());
                    else
                        return nullptr;
                }
                break;
            }
            case TransformFunctionParameterType::LengthPercentage: {
                if (maybe_calc_value && maybe_calc_value->resolves_to_length()) {
                    values.append(maybe_calc_value.release_nonnull());
                } else {
                    auto dimension_value = parse_dimension_value(value);
                    if (!dimension_value)
                        return nullptr;

                    if (dimension_value->is_percentage() || dimension_value->is_length())
                        values.append(dimension_value.release_nonnull());
                    else
                        return nullptr;
                }
                break;
            }
            case TransformFunctionParameterType::Number: {
                if (maybe_calc_value && maybe_calc_value->resolves_to_number()) {
                    values.append(maybe_calc_value.release_nonnull());
                } else {
                    // FIXME: Remove this reconsume once all parsing functions are TokenStream-based.
                    argument_tokens.reconsume_current_input_token();
                    auto number = parse_number_value(argument_tokens);
                    if (!number)
                        return nullptr;
                    values.append(number.release_nonnull());
                }
                break;
            }
            }

            argument_tokens.skip_whitespace();
            if (argument_tokens.has_next_token()) {
                // Arguments must be separated by commas.
                if (!argument_tokens.next_token().is(Token::Type::Comma))
                    return nullptr;
                argument_tokens.skip_whitespace();

                // If there are no more parameters after the comma, this is invalid.
                if (!argument_tokens.has_next_token())
                    return nullptr;
            }

            argument_index++;
        }

        if (argument_index < function_metadata.parameters.size() && function_metadata.parameters[argument_index].required) {
            dbgln_if(CSS_PARSER_DEBUG, "Required parameter at position {} is missing", argument_index);
            return nullptr;
        }

        transformations.append(TransformationStyleValue::create(function, move(values)));
    }
    return StyleValueList::create(move(transformations), StyleValueList::Separator::Space);
}

// https://www.w3.org/TR/css-transforms-1/#propdef-transform-origin
// FIXME: This only supports a 2D position
RefPtr<StyleValue> Parser::parse_transform_origin_value(Vector<ComponentValue> const& component_values)
{
    enum class Axis {
        None,
        X,
        Y,
    };

    struct AxisOffset {
        Axis axis;
        NonnullRefPtr<StyleValue> offset;
    };

    auto to_axis_offset = [](RefPtr<StyleValue> value) -> Optional<AxisOffset> {
        if (value->is_percentage())
            return AxisOffset { Axis::None, value->as_percentage() };
        if (value->is_length())
            return AxisOffset { Axis::None, value->as_length() };
        if (value->is_identifier()) {
            switch (value->to_identifier()) {
            case ValueID::Top:
                return AxisOffset { Axis::Y, PercentageStyleValue::create(Percentage(0)) };
            case ValueID::Left:
                return AxisOffset { Axis::X, PercentageStyleValue::create(Percentage(0)) };
            case ValueID::Center:
                return AxisOffset { Axis::None, PercentageStyleValue::create(Percentage(50)) };
            case ValueID::Bottom:
                return AxisOffset { Axis::Y, PercentageStyleValue::create(Percentage(100)) };
            case ValueID::Right:
                return AxisOffset { Axis::X, PercentageStyleValue::create(Percentage(100)) };
            default:
                return OptionalNone {};
            }
        }
        return OptionalNone {};
    };

    auto make_list = [](NonnullRefPtr<StyleValue> const& x_value, NonnullRefPtr<StyleValue> const& y_value) -> NonnullRefPtr<StyleValueList> {
        StyleValueVector values;
        values.append(x_value);
        values.append(y_value);
        return StyleValueList::create(move(values), StyleValueList::Separator::Space);
    };

    auto tokens = TokenStream { component_values };
    switch (component_values.size()) {
    case 1: {
        auto single_value = to_axis_offset(parse_css_value_for_property(PropertyID::TransformOrigin, tokens));
        if (!single_value.has_value())
            return nullptr;
        // If only one value is specified, the second value is assumed to be center.
        // FIXME: If one or two values are specified, the third value is assumed to be 0px.
        switch (single_value->axis) {
        case Axis::None:
        case Axis::X:
            return make_list(single_value->offset, PercentageStyleValue::create(Percentage(50)));
        case Axis::Y:
            return make_list(PercentageStyleValue::create(Percentage(50)), single_value->offset);
        }
        VERIFY_NOT_REACHED();
    }
    case 2: {
        auto first_value = to_axis_offset(parse_css_value_for_property(PropertyID::TransformOrigin, tokens));
        auto second_value = to_axis_offset(parse_css_value_for_property(PropertyID::TransformOrigin, tokens));
        if (!first_value.has_value() || !second_value.has_value())
            return nullptr;

        RefPtr<StyleValue> x_value;
        RefPtr<StyleValue> y_value;

        if (first_value->axis == Axis::X) {
            x_value = first_value->offset;
        } else if (first_value->axis == Axis::Y) {
            y_value = first_value->offset;
        }

        if (second_value->axis == Axis::X) {
            if (x_value)
                return nullptr;
            x_value = second_value->offset;
            // Put the other in Y since its axis can't have been X
            y_value = first_value->offset;
        } else if (second_value->axis == Axis::Y) {
            if (y_value)
                return nullptr;
            y_value = second_value->offset;
            // Put the other in X since its axis can't have been Y
            x_value = first_value->offset;
        } else {
            if (x_value) {
                VERIFY(!y_value);
                y_value = second_value->offset;
            } else {
                VERIFY(!x_value);
                x_value = second_value->offset;
            }
        }
        // If two or more values are defined and either no value is a keyword, or the only used keyword is center,
        // then the first value represents the horizontal position (or offset) and the second represents the vertical position (or offset).
        // FIXME: A third value always represents the Z position (or offset) and must be of type <length>.
        if (first_value->axis == Axis::None && second_value->axis == Axis::None) {
            x_value = first_value->offset;
            y_value = second_value->offset;
        }
        return make_list(x_value.release_nonnull(), y_value.release_nonnull());
    }
    }

    return nullptr;
}

RefPtr<StyleValue> Parser::parse_as_css_value(PropertyID property_id)
{
    auto component_values = parse_a_list_of_component_values(m_token_stream);
    auto tokens = TokenStream(component_values);
    auto parsed_value = parse_css_value(property_id, tokens);
    if (parsed_value.is_error())
        return nullptr;
    return parsed_value.release_value();
}

Optional<CSS::GridSize> Parser::parse_grid_size(ComponentValue const& component_value)
{
    if (component_value.is_function()) {
        if (auto maybe_calculated = parse_calculated_value(component_value))
            return GridSize(LengthPercentage(maybe_calculated.release_nonnull()));

        return {};
    }
    auto token = component_value.token();
    if (token.is(Token::Type::Dimension) && token.dimension_unit().equals_ignoring_ascii_case("fr"sv)) {
        auto numeric_value = token.dimension_value();
        if (numeric_value)
            return GridSize(numeric_value);
    }
    if (token.is(Token::Type::Ident) && token.ident().equals_ignoring_ascii_case("auto"sv))
        return GridSize::make_auto();
    if (token.is(Token::Type::Ident) && token.ident().equals_ignoring_ascii_case("max-content"sv))
        return GridSize(GridSize::Type::MaxContent);
    if (token.is(Token::Type::Ident) && token.ident().equals_ignoring_ascii_case("min-content"sv))
        return GridSize(GridSize::Type::MinContent);
    auto dimension = parse_dimension(token);
    if (!dimension.has_value())
        return {};
    if (dimension->is_length())
        return GridSize(dimension->length());
    else if (dimension->is_percentage())
        return GridSize(dimension->percentage());
    return {};
}

Optional<CSS::GridMinMax> Parser::parse_min_max(Vector<ComponentValue> const& component_values)
{
    // https://www.w3.org/TR/css-grid-2/#valdef-grid-template-columns-minmax
    // 'minmax(min, max)'
    // Defines a size range greater than or equal to min and less than or equal to max. If the max is
    // less than the min, then the max will be floored by the min (essentially yielding minmax(min,
    // min)). As a maximum, a <flex> value sets the track’s flex factor; it is invalid as a minimum.
    auto function_tokens = TokenStream(component_values);
    auto comma_separated_list = parse_a_comma_separated_list_of_component_values(function_tokens);
    if (comma_separated_list.size() != 2)
        return {};

    TokenStream part_one_tokens { comma_separated_list[0] };
    part_one_tokens.skip_whitespace();
    if (!part_one_tokens.has_next_token())
        return {};
    auto current_token = part_one_tokens.next_token();
    auto min_grid_size = parse_grid_size(current_token);

    TokenStream part_two_tokens { comma_separated_list[1] };
    part_two_tokens.skip_whitespace();
    if (!part_two_tokens.has_next_token())
        return {};
    current_token = part_two_tokens.next_token();
    auto max_grid_size = parse_grid_size(current_token);

    if (min_grid_size.has_value() && max_grid_size.has_value()) {
        // https://www.w3.org/TR/css-grid-2/#valdef-grid-template-columns-minmax
        // As a maximum, a <flex> value sets the track’s flex factor; it is invalid as a minimum.
        if (min_grid_size.value().is_flexible_length())
            return {};
        return CSS::GridMinMax(min_grid_size.value(), max_grid_size.value());
    }
    return {};
}

Optional<CSS::GridRepeat> Parser::parse_repeat(Vector<ComponentValue> const& component_values)
{
    // https://www.w3.org/TR/css-grid-2/#repeat-syntax
    // 7.2.3.1. Syntax of repeat()
    // The generic form of the repeat() syntax is, approximately,
    // repeat( [ <integer [1,∞]> | auto-fill | auto-fit ] , <track-list> )
    auto is_auto_fill = false;
    auto is_auto_fit = false;
    auto function_tokens = TokenStream(component_values);
    auto comma_separated_list = parse_a_comma_separated_list_of_component_values(function_tokens);
    if (comma_separated_list.size() != 2)
        return {};
    // The first argument specifies the number of repetitions.
    TokenStream part_one_tokens { comma_separated_list[0] };
    part_one_tokens.skip_whitespace();
    if (!part_one_tokens.has_next_token())
        return {};
    auto current_token = part_one_tokens.next_token().token();

    auto repeat_count = 0;
    if (current_token.is(Token::Type::Number) && current_token.number().is_integer() && current_token.number_value() > 0)
        repeat_count = current_token.number_value();
    else if (current_token.is(Token::Type::Ident) && current_token.ident().equals_ignoring_ascii_case("auto-fill"sv))
        is_auto_fill = true;
    else if (current_token.is(Token::Type::Ident) && current_token.ident().equals_ignoring_ascii_case("auto-fit"sv))
        is_auto_fit = true;

    // The second argument is a track list, which is repeated that number of times.
    TokenStream part_two_tokens { comma_separated_list[1] };
    part_two_tokens.skip_whitespace();
    if (!part_two_tokens.has_next_token())
        return {};

    Vector<CSS::ExplicitGridTrack> repeat_params;
    Vector<Vector<String>> line_names_list;
    auto last_object_was_line_names = false;
    while (part_two_tokens.has_next_token()) {
        auto token = part_two_tokens.next_token();
        Vector<String> line_names;
        if (token.is_block()) {
            if (last_object_was_line_names)
                return {};
            last_object_was_line_names = true;
            if (!token.block().is_square())
                return {};
            TokenStream block_tokens { token.block().values() };
            while (block_tokens.has_next_token()) {
                auto current_block_token = block_tokens.next_token();
                auto maybe_string = String::from_utf8(current_block_token.token().ident());
                if (maybe_string.is_error())
                    return {};
                line_names.append(maybe_string.value());
                block_tokens.skip_whitespace();
            }
            line_names_list.append(line_names);
            part_two_tokens.skip_whitespace();
        } else {
            last_object_was_line_names = false;
            auto track_sizing_function = parse_track_sizing_function(token);
            if (!track_sizing_function.has_value())
                return {};
            // However, there are some restrictions:
            // The repeat() notation can’t be nested.
            if (track_sizing_function.value().is_repeat())
                return {};
            // Automatic repetitions (auto-fill or auto-fit) cannot be combined with intrinsic or flexible sizes.
            if (track_sizing_function.value().is_default() && track_sizing_function.value().grid_size().is_flexible_length() && (is_auto_fill || is_auto_fit))
                return {};
            repeat_params.append(track_sizing_function.value());
            part_two_tokens.skip_whitespace();
        }
    }
    while (line_names_list.size() <= repeat_params.size())
        line_names_list.append({});

    // Thus the precise syntax of the repeat() notation has several forms:
    // <track-repeat> = repeat( [ <integer [1,∞]> ] , [ <line-names>? <track-size> ]+ <line-names>? )
    // <auto-repeat>  = repeat( [ auto-fill | auto-fit ] , [ <line-names>? <fixed-size> ]+ <line-names>? )
    // <fixed-repeat> = repeat( [ <integer [1,∞]> ] , [ <line-names>? <fixed-size> ]+ <line-names>? )
    // <name-repeat>  = repeat( [ <integer [1,∞]> | auto-fill ], <line-names>+)

    // The <track-repeat> variant can represent the repetition of any <track-size>, but is limited to a
    // fixed number of repetitions.

    // The <auto-repeat> variant can repeat automatically to fill a space, but requires definite track
    // sizes so that the number of repetitions can be calculated. It can only appear once in the track
    // list, but the same track list can also contain <fixed-repeat>s.

    // The <name-repeat> variant is for adding line names to subgrids. It can only be used with the
    // subgrid keyword and cannot specify track sizes, only line names.

    // If a repeat() function that is not a <name-repeat> ends up placing two <line-names> adjacent to
    // each other, the name lists are merged. For example, repeat(2, [a] 1fr [b]) is equivalent to [a]
    // 1fr [b a] 1fr [b].
    if (is_auto_fill)
        return CSS::GridRepeat(CSS::GridTrackSizeList(repeat_params, line_names_list), CSS::GridRepeat::Type::AutoFill);
    else if (is_auto_fit)
        return CSS::GridRepeat(CSS::GridTrackSizeList(repeat_params, line_names_list), CSS::GridRepeat::Type::AutoFit);
    else
        return CSS::GridRepeat(CSS::GridTrackSizeList(repeat_params, line_names_list), repeat_count);
}

Optional<CSS::ExplicitGridTrack> Parser::parse_track_sizing_function(ComponentValue const& token)
{
    if (token.is_function()) {
        auto const& function_token = token.function();
        if (function_token.name().equals_ignoring_ascii_case("repeat"sv)) {
            auto maybe_repeat = parse_repeat(function_token.values());
            if (maybe_repeat.has_value())
                return CSS::ExplicitGridTrack(maybe_repeat.value());
            else
                return {};
        } else if (function_token.name().equals_ignoring_ascii_case("minmax"sv)) {
            auto maybe_min_max_value = parse_min_max(function_token.values());
            if (maybe_min_max_value.has_value())
                return CSS::ExplicitGridTrack(maybe_min_max_value.value());
            else
                return {};
        } else if (auto maybe_calculated = parse_calculated_value(token)) {
            return CSS::ExplicitGridTrack(GridSize(LengthPercentage(maybe_calculated.release_nonnull())));
        }
        return {};
    } else if (token.is(Token::Type::Ident) && token.token().ident().equals_ignoring_ascii_case("auto"sv)) {
        return CSS::ExplicitGridTrack(GridSize(Length::make_auto()));
    } else if (token.is_block()) {
        return {};
    } else {
        auto grid_size = parse_grid_size(token);
        if (!grid_size.has_value())
            return {};
        return CSS::ExplicitGridTrack(grid_size.value());
    }
}

RefPtr<StyleValue> Parser::parse_grid_track_size_list(Vector<ComponentValue> const& component_values, bool allow_separate_line_name_blocks)
{
    if (component_values.size() == 1 && component_values.first().is(Token::Type::Ident)) {
        auto ident = parse_identifier_value(component_values.first());
        if (ident && ident->to_identifier() == ValueID::None) {
            return GridTrackSizeListStyleValue::make_none();
        }
    }

    Vector<CSS::ExplicitGridTrack> track_list;
    Vector<Vector<String>> line_names_list;
    auto last_object_was_line_names = false;
    TokenStream tokens { component_values };
    while (tokens.has_next_token()) {
        auto token = tokens.next_token();
        if (token.is_block()) {
            if (last_object_was_line_names && !allow_separate_line_name_blocks)
                return GridTrackSizeListStyleValue::make_auto();
            last_object_was_line_names = true;
            Vector<String> line_names;
            if (!token.block().is_square())
                return GridTrackSizeListStyleValue::make_auto();
            TokenStream block_tokens { token.block().values() };
            block_tokens.skip_whitespace();
            while (block_tokens.has_next_token()) {
                auto current_block_token = block_tokens.next_token();
                auto maybe_string = String::from_utf8(current_block_token.token().ident());
                if (maybe_string.is_error())
                    return nullptr;
                line_names.append(maybe_string.value());
                block_tokens.skip_whitespace();
            }
            line_names_list.append(line_names);
        } else {
            last_object_was_line_names = false;
            auto track_sizing_function = parse_track_sizing_function(token);
            if (!track_sizing_function.has_value())
                return GridTrackSizeListStyleValue::make_auto();
            // FIXME: Handle multiple repeat values (should combine them here, or remove
            // any other ones if the first one is auto-fill, etc.)
            track_list.append(track_sizing_function.value());
        }
    }
    while (line_names_list.size() <= track_list.size())
        line_names_list.append({});
    return GridTrackSizeListStyleValue::create(CSS::GridTrackSizeList(track_list, line_names_list));
}

// https://www.w3.org/TR/css-grid-1/#grid-auto-flow-property
RefPtr<GridAutoFlowStyleValue> Parser::parse_grid_auto_flow_value(Vector<ComponentValue> const& component_values)
{
    // [ row | column ] || dense
    TokenStream<ComponentValue> tokens { component_values };
    if (!tokens.has_next_token())
        return nullptr;

    auto parse_axis = [&]() -> Optional<GridAutoFlowStyleValue::Axis> {
        auto transaction = tokens.begin_transaction();
        auto token = tokens.next_token();
        if (!token.is(Token::Type::Ident))
            return {};
        auto const& ident = token.token().ident();
        if (ident.equals_ignoring_ascii_case("row"sv)) {
            transaction.commit();
            return GridAutoFlowStyleValue::Axis::Row;
        } else if (ident.equals_ignoring_ascii_case("column"sv)) {
            transaction.commit();
            return GridAutoFlowStyleValue::Axis::Column;
        }
        return {};
    };

    auto parse_dense = [&]() -> Optional<GridAutoFlowStyleValue::Dense> {
        auto transaction = tokens.begin_transaction();
        auto token = tokens.next_token();
        if (!token.is(Token::Type::Ident))
            return {};
        auto const& ident = token.token().ident();
        if (ident.equals_ignoring_ascii_case("dense"sv)) {
            transaction.commit();
            return GridAutoFlowStyleValue::Dense::Yes;
        }
        return {};
    };

    Optional<GridAutoFlowStyleValue::Axis> axis;
    Optional<GridAutoFlowStyleValue::Dense> dense;
    if (axis = parse_axis(); axis.has_value()) {
        dense = parse_dense();
    } else if (dense = parse_dense(); dense.has_value()) {
        axis = parse_axis();
    }

    if (tokens.has_next_token())
        return nullptr;

    return GridAutoFlowStyleValue::create(axis.value_or(GridAutoFlowStyleValue::Axis::Row), dense.value_or(GridAutoFlowStyleValue::Dense::No));
}

RefPtr<StyleValue> Parser::parse_grid_auto_track_sizes(Vector<ComponentValue> const& component_values)
{
    // https://www.w3.org/TR/css-grid-2/#auto-tracks
    // <track-size>+
    Vector<CSS::ExplicitGridTrack> track_list;
    TokenStream tokens { component_values };
    while (tokens.has_next_token()) {
        auto token = tokens.next_token();
        auto track_sizing_function = parse_track_sizing_function(token);
        if (!track_sizing_function.has_value())
            return GridTrackSizeListStyleValue::make_auto();
        // FIXME: Handle multiple repeat values (should combine them here, or remove
        // any other ones if the first one is auto-fill, etc.)
        track_list.append(track_sizing_function.value());
    }
    return GridTrackSizeListStyleValue::create(CSS::GridTrackSizeList(track_list, {}));
}

RefPtr<StyleValue> Parser::parse_grid_track_placement(Vector<ComponentValue> const& component_values)
{
    // https://www.w3.org/TR/css-grid-2/#line-placement
    // Line-based Placement: the grid-row-start, grid-column-start, grid-row-end, and grid-column-end properties
    // <grid-line> =
    //     auto |
    //     <custom-ident> |
    //     [ <integer> && <custom-ident>? ] |
    //     [ span && [ <integer> || <custom-ident> ] ]
    auto is_auto = [](Token token) -> bool {
        if (token.is(Token::Type::Ident) && token.ident().equals_ignoring_ascii_case("auto"sv))
            return true;
        return false;
    };
    auto is_span = [](Token token) -> bool {
        if (token.is(Token::Type::Ident) && token.ident().equals_ignoring_ascii_case("span"sv))
            return true;
        return false;
    };
    auto is_valid_integer = [](Token token) -> bool {
        // An <integer> value of zero makes the declaration invalid.
        if (token.is(Token::Type::Number) && token.number().is_integer() && token.number_value() != 0)
            return true;
        return false;
    };
    auto is_identifier = [](Token token) -> bool {
        // The <custom-ident> additionally excludes the keywords span and auto.
        if (token.is(Token::Type::Ident) && !token.ident().equals_ignoring_ascii_case("span"sv) && !token.ident().equals_ignoring_ascii_case("auto"sv))
            return true;
        return false;
    };

    auto tokens = TokenStream { component_values };
    tokens.skip_whitespace();
    auto current_token = tokens.next_token().token();

    if (!tokens.has_next_token()) {
        if (is_auto(current_token))
            return GridTrackPlacementStyleValue::create(GridTrackPlacement::make_auto());
        if (is_span(current_token))
            return GridTrackPlacementStyleValue::create(GridTrackPlacement::make_span(1));
        if (is_valid_integer(current_token))
            return GridTrackPlacementStyleValue::create(GridTrackPlacement::make_line(static_cast<int>(current_token.number_value()), {}));
        if (is_identifier(current_token)) {
            auto maybe_string = String::from_utf8(current_token.ident());
            if (!maybe_string.is_error())
                return GridTrackPlacementStyleValue::create(GridTrackPlacement::make_line({}, maybe_string.value()));
        }
        return nullptr;
    }

    auto span_value = false;
    auto span_or_position_value = 0;
    String identifier_value;
    while (true) {
        if (is_auto(current_token))
            return nullptr;
        if (is_span(current_token)) {
            if (span_value == false)
                span_value = true;
            else
                return nullptr;
        }
        if (is_valid_integer(current_token)) {
            if (span_or_position_value == 0)
                span_or_position_value = static_cast<int>(current_token.number_value());
            else
                return nullptr;
        }
        if (is_identifier(current_token)) {
            if (identifier_value.is_empty()) {
                auto maybe_string = String::from_utf8(current_token.ident());
                if (maybe_string.is_error())
                    return nullptr;
                identifier_value = maybe_string.release_value();
            } else {
                return nullptr;
            }
        }
        tokens.skip_whitespace();
        if (tokens.has_next_token())
            current_token = tokens.next_token().token();
        else
            break;
    }

    // Negative integers or zero are invalid.
    if (span_value && span_or_position_value < 1)
        return nullptr;

    // If the <integer> is omitted, it defaults to 1.
    if (span_or_position_value == 0)
        span_or_position_value = 1;

    if (!identifier_value.is_empty())
        return GridTrackPlacementStyleValue::create(GridTrackPlacement::make_line(span_or_position_value, identifier_value));
    return GridTrackPlacementStyleValue::create(GridTrackPlacement::make_span(span_or_position_value));
}

RefPtr<StyleValue> Parser::parse_grid_track_placement_shorthand_value(Vector<ComponentValue> const& component_values)
{
    auto tokens = TokenStream { component_values };
    auto current_token = tokens.next_token().token();

    Vector<ComponentValue> track_start_placement_tokens;
    while (true) {
        if (current_token.is(Token::Type::Delim) && current_token.delim() == "/"sv)
            break;
        track_start_placement_tokens.append(current_token);
        if (!tokens.has_next_token())
            break;
        current_token = tokens.next_token().token();
    }

    Vector<ComponentValue> track_end_placement_tokens;
    if (tokens.has_next_token()) {
        current_token = tokens.next_token().token();
        while (true) {
            track_end_placement_tokens.append(current_token);
            if (!tokens.has_next_token())
                break;
            current_token = tokens.next_token().token();
        }
    }

    auto parsed_start_value = parse_grid_track_placement(track_start_placement_tokens);
    if (parsed_start_value && track_end_placement_tokens.is_empty())
        return GridTrackPlacementShorthandStyleValue::create(parsed_start_value.release_nonnull()->as_grid_track_placement().grid_track_placement());

    auto parsed_end_value = parse_grid_track_placement(track_end_placement_tokens);
    if (parsed_start_value && parsed_end_value)
        return GridTrackPlacementShorthandStyleValue::create(parsed_start_value.release_nonnull()->as_grid_track_placement(), parsed_end_value.release_nonnull()->as_grid_track_placement());

    return nullptr;
}

// https://www.w3.org/TR/css-grid-2/#explicit-grid-shorthand
// 7.4. Explicit Grid Shorthand: the grid-template property
RefPtr<StyleValue> Parser::parse_grid_track_size_list_shorthand_value(Vector<ComponentValue> const& component_values)
{
    // The grid-template property is a shorthand for setting grid-template-columns, grid-template-rows,
    // and grid-template-areas in a single declaration. It has several distinct syntax forms:
    // none
    //    - Sets all three properties to their initial values (none).
    // <'grid-template-rows'> / <'grid-template-columns'>
    //    - Sets grid-template-rows and grid-template-columns to the specified values, respectively, and sets grid-template-areas to none.
    // [ <line-names>? <string> <track-size>? <line-names>? ]+ [ / <explicit-track-list> ]?
    //    - Sets grid-template-areas to the strings listed.
    //    - Sets grid-template-rows to the <track-size>s following each string (filling in auto for any missing sizes),
    //      and splicing in the named lines defined before/after each size.
    //    - Sets grid-template-columns to the track listing specified after the slash (or none, if not specified).
    Vector<ComponentValue> template_rows_tokens;
    Vector<ComponentValue> template_columns_tokens;
    Vector<ComponentValue> template_area_tokens;

    int forward_slash_index = -1;
    for (size_t x = 0; x < component_values.size(); x++) {
        if (component_values[x].is_delim('/')) {
            forward_slash_index = x;
            break;
        }
    }

    for (size_t x = 0; x < (forward_slash_index > -1 ? forward_slash_index : component_values.size()); x++) {
        if (component_values[x].is_token() && component_values[x].token().is(Token::Type::String))
            template_area_tokens.append(component_values[x]);
        else
            template_rows_tokens.append(component_values[x]);
    }
    if (forward_slash_index > -1) {
        for (size_t x = forward_slash_index + 1; x < component_values.size(); x++)
            template_columns_tokens.append(component_values[x]);
    }

    auto parsed_template_areas_values = parse_grid_template_areas_value(template_area_tokens);
    auto parsed_template_rows_values = parse_grid_track_size_list(template_rows_tokens, true);
    auto parsed_template_columns_values = parse_grid_track_size_list(template_columns_tokens);
    return GridTrackSizeListShorthandStyleValue::create(
        parsed_template_areas_values.release_nonnull()->as_grid_template_area(),
        parsed_template_rows_values.release_nonnull()->as_grid_track_size_list(),
        parsed_template_columns_values.release_nonnull()->as_grid_track_size_list());
}

RefPtr<StyleValue> Parser::parse_grid_area_shorthand_value(Vector<ComponentValue> const& component_values)
{
    auto tokens = TokenStream { component_values };
    Token current_token;

    auto parse_placement_tokens = [&](Vector<ComponentValue>& placement_tokens, bool check_for_delimiter = true) -> void {
        current_token = tokens.next_token().token();
        while (true) {
            if (check_for_delimiter && current_token.is(Token::Type::Delim) && current_token.delim() == "/"sv)
                break;
            placement_tokens.append(current_token);
            tokens.skip_whitespace();
            if (!tokens.has_next_token())
                break;
            current_token = tokens.next_token().token();
        }
    };

    Vector<ComponentValue> row_start_placement_tokens;
    parse_placement_tokens(row_start_placement_tokens);

    Vector<ComponentValue> column_start_placement_tokens;
    if (tokens.has_next_token())
        parse_placement_tokens(column_start_placement_tokens);

    Vector<ComponentValue> row_end_placement_tokens;
    if (tokens.has_next_token())
        parse_placement_tokens(row_end_placement_tokens);

    Vector<ComponentValue> column_end_placement_tokens;
    if (tokens.has_next_token())
        parse_placement_tokens(column_end_placement_tokens, false);

    // https://www.w3.org/TR/css-grid-2/#placement-shorthands
    // The grid-area property is a shorthand for grid-row-start, grid-column-start, grid-row-end and
    // grid-column-end.
    auto row_start_style_value = parse_grid_track_placement(row_start_placement_tokens);
    auto column_start_style_value = parse_grid_track_placement(column_start_placement_tokens);
    auto row_end_style_value = parse_grid_track_placement(row_end_placement_tokens);
    auto column_end_style_value = parse_grid_track_placement(column_end_placement_tokens);

    // If four <grid-line> values are specified, grid-row-start is set to the first value, grid-column-start
    // is set to the second value, grid-row-end is set to the third value, and grid-column-end is set to the
    // fourth value.
    auto row_start = GridTrackPlacement::make_auto();
    auto column_start = GridTrackPlacement::make_auto();
    auto row_end = GridTrackPlacement::make_auto();
    auto column_end = GridTrackPlacement::make_auto();

    if (row_start_style_value)
        row_start = row_start_style_value.release_nonnull()->as_grid_track_placement().grid_track_placement();

    // When grid-column-start is omitted, if grid-row-start is a <custom-ident>, all four longhands are set to
    // that value. Otherwise, it is set to auto.
    if (column_start_style_value)
        column_start = column_start_style_value.release_nonnull()->as_grid_track_placement().grid_track_placement();
    else
        column_start = row_start;

    // When grid-row-end is omitted, if grid-row-start is a <custom-ident>, grid-row-end is set to that
    // <custom-ident>; otherwise, it is set to auto.
    if (row_end_style_value)
        row_end = row_end_style_value.release_nonnull()->as_grid_track_placement().grid_track_placement();
    else
        row_end = column_start;

    // When grid-column-end is omitted, if grid-column-start is a <custom-ident>, grid-column-end is set to
    // that <custom-ident>; otherwise, it is set to auto.
    if (column_end_style_value)
        column_end = column_end_style_value.release_nonnull()->as_grid_track_placement().grid_track_placement();
    else
        column_end = row_end;

    return GridAreaShorthandStyleValue::create(row_start, column_start, row_end, column_end);
}

RefPtr<StyleValue> Parser::parse_grid_shorthand_value(Vector<ComponentValue> const& component_value)
{
    // <'grid-template'> |
    // FIXME: <'grid-template-rows'> / [ auto-flow && dense? ] <'grid-auto-columns'>? |
    // FIXME: [ auto-flow && dense? ] <'grid-auto-rows'>? / <'grid-template-columns'>
    return parse_grid_track_size_list_shorthand_value(component_value);
}

RefPtr<StyleValue> Parser::parse_grid_template_areas_value(Vector<ComponentValue> const& component_values)
{
    Vector<Vector<String>> grid_area_rows;
    for (auto& component_value : component_values) {
        Vector<String> grid_area_columns;
        if (component_value.is(Token::Type::String)) {
            auto const parts = MUST(MUST(String::from_utf8(component_value.token().string())).split(' '));
            for (auto& part : parts) {
                grid_area_columns.append(part);
            }
        }
        grid_area_rows.append(move(grid_area_columns));
    }
    return GridTemplateAreaStyleValue::create(grid_area_rows);
}

static bool block_contains_var_or_attr(Block const& block);

static bool function_contains_var_or_attr(Function const& function)
{
    if (function.name().equals_ignoring_ascii_case("var"sv) || function.name().equals_ignoring_ascii_case("attr"sv))
        return true;
    for (auto const& token : function.values()) {
        if (token.is_function() && function_contains_var_or_attr(token.function()))
            return true;
        if (token.is_block() && block_contains_var_or_attr(token.block()))
            return true;
    }
    return false;
}

bool block_contains_var_or_attr(Block const& block)
{
    for (auto const& token : block.values()) {
        if (token.is_function() && function_contains_var_or_attr(token.function()))
            return true;
        if (token.is_block() && block_contains_var_or_attr(token.block()))
            return true;
    }
    return false;
}

Parser::ParseErrorOr<NonnullRefPtr<StyleValue>> Parser::parse_css_value(PropertyID property_id, TokenStream<ComponentValue>& tokens)
{
    m_context.set_current_property_id(property_id);
    Vector<ComponentValue> component_values;
    bool contains_var_or_attr = false;
    bool const property_accepts_custom_ident = property_accepts_type(property_id, ValueType::CustomIdent);

    while (tokens.has_next_token()) {
        auto const& token = tokens.next_token();

        if (token.is(Token::Type::Semicolon)) {
            tokens.reconsume_current_input_token();
            break;
        }

        if (property_id != PropertyID::Custom) {
            if (token.is(Token::Type::Whitespace))
                continue;

            if (!property_accepts_custom_ident && token.is(Token::Type::Ident) && has_ignored_vendor_prefix(token.token().ident()))
                return ParseError::IncludesIgnoredVendorPrefix;
        }

        if (!contains_var_or_attr) {
            if (token.is_function() && function_contains_var_or_attr(token.function()))
                contains_var_or_attr = true;
            else if (token.is_block() && block_contains_var_or_attr(token.block()))
                contains_var_or_attr = true;
        }

        component_values.append(token);
    }

    if (property_id == PropertyID::Custom || contains_var_or_attr)
        return UnresolvedStyleValue::create(move(component_values), contains_var_or_attr);

    if (component_values.is_empty())
        return ParseError::SyntaxError;

    if (component_values.size() == 1) {
        if (auto parsed_value = parse_builtin_value(component_values.first()))
            return parsed_value.release_nonnull();
    }

    // Special-case property handling
    switch (property_id) {
    case PropertyID::AspectRatio:
        if (auto parsed_value = parse_aspect_ratio_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BackdropFilter:
        if (auto parsed_value = parse_filter_value_list_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Background:
        if (auto parsed_value = parse_background_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BackgroundAttachment:
    case PropertyID::BackgroundClip:
    case PropertyID::BackgroundImage:
    case PropertyID::BackgroundOrigin:
        if (auto parsed_value = parse_simple_comma_separated_value_list(property_id, component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BackgroundPosition:
        if (auto parsed_value = parse_comma_separated_value_list(component_values, [this](auto& tokens) { return parse_single_background_position_value(tokens); }))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BackgroundPositionX:
    case PropertyID::BackgroundPositionY:
        if (auto parsed_value = parse_comma_separated_value_list(component_values, [this, property_id](auto& tokens) { return parse_single_background_position_x_or_y_value(tokens, property_id); }))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BackgroundRepeat:
        if (auto parsed_value = parse_comma_separated_value_list(component_values, [this](auto& tokens) { return parse_single_background_repeat_value(tokens); }))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BackgroundSize:
        if (auto parsed_value = parse_comma_separated_value_list(component_values, [this](auto& tokens) { return parse_single_background_size_value(tokens); }))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Border:
    case PropertyID::BorderBottom:
    case PropertyID::BorderLeft:
    case PropertyID::BorderRight:
    case PropertyID::BorderTop:
        if (auto parsed_value = parse_border_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BorderTopLeftRadius:
    case PropertyID::BorderTopRightRadius:
    case PropertyID::BorderBottomRightRadius:
    case PropertyID::BorderBottomLeftRadius:
        if (auto parsed_value = parse_border_radius_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BorderRadius:
        if (auto parsed_value = parse_border_radius_shorthand_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::BoxShadow:
        if (auto parsed_value = parse_shadow_value(component_values, AllowInsetKeyword::Yes))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Content:
        if (auto parsed_value = parse_content_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Display:
        if (auto parsed_value = parse_display_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Flex:
        if (auto parsed_value = parse_flex_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::FlexFlow:
        if (auto parsed_value = parse_flex_flow_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Font:
        if (auto parsed_value = parse_font_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::FontFamily: {
        auto tokens_without_whitespace = TokenStream { component_values };
        if (auto parsed_value = parse_font_family_value(tokens_without_whitespace))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    }
    case PropertyID::GridColumn:
        if (auto parsed_value = parse_grid_track_placement_shorthand_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridArea:
        if (auto parsed_value = parse_grid_area_shorthand_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridAutoFlow:
        if (auto parsed_value = parse_grid_auto_flow_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridTemplateAreas:
        if (auto parsed_value = parse_grid_template_areas_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridColumnEnd:
        if (auto parsed_value = parse_grid_track_placement(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridColumnStart:
        if (auto parsed_value = parse_grid_track_placement(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridRow:
        if (auto parsed_value = parse_grid_track_placement_shorthand_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridRowEnd:
        if (auto parsed_value = parse_grid_track_placement(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridRowStart:
        if (auto parsed_value = parse_grid_track_placement(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Grid:
        if (auto parsed_value = parse_grid_shorthand_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridTemplate:
        if (auto parsed_value = parse_grid_track_size_list_shorthand_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridTemplateColumns:
        if (auto parsed_value = parse_grid_track_size_list(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridTemplateRows:
        if (auto parsed_value = parse_grid_track_size_list(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridAutoColumns:
        if (auto parsed_value = parse_grid_auto_track_sizes(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::GridAutoRows:
        if (auto parsed_value = parse_grid_auto_track_sizes(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::ListStyle:
        if (auto parsed_value = parse_list_style_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Overflow:
        if (auto parsed_value = parse_overflow_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::PlaceContent:
        if (auto parsed_value = parse_place_content_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::PlaceItems:
        if (auto parsed_value = parse_place_items_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::PlaceSelf:
        if (auto parsed_value = parse_place_self_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::TextDecoration:
        if (auto parsed_value = parse_text_decoration_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::TextDecorationLine: {
        TokenStream value_tokens { component_values };
        auto parsed_value = parse_text_decoration_line_value(value_tokens);
        if (parsed_value && !value_tokens.has_next_token())
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    }
    case PropertyID::TextShadow:
        if (auto parsed_value = parse_shadow_value(component_values, AllowInsetKeyword::No))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::Transform:
        if (auto parsed_value = parse_transform_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError::SyntaxError;
    case PropertyID::TransformOrigin:
        if (auto parsed_value = parse_transform_origin_value(component_values))
            return parsed_value.release_nonnull();
        return ParseError ::SyntaxError;
    default:
        break;
    }

    // If there's only 1 ComponentValue, we can only produce a single StyleValue.
    if (component_values.size() == 1) {
        auto stream = TokenStream { component_values };
        if (auto parsed_value = parse_css_value_for_property(property_id, stream))
            return parsed_value.release_nonnull();
    } else {
        StyleValueVector parsed_values;
        auto stream = TokenStream { component_values };
        while (auto parsed_value = parse_css_value_for_property(property_id, stream)) {
            parsed_values.append(parsed_value.release_nonnull());
            if (!stream.has_next_token())
                break;
        }

        // Some types (such as <ratio>) can be made from multiple ComponentValues, so if we only made 1 StyleValue, return it directly.
        if (parsed_values.size() == 1)
            return *parsed_values.take_first();

        if (!parsed_values.is_empty() && parsed_values.size() <= property_maximum_value_count(property_id))
            return StyleValueList::create(move(parsed_values), StyleValueList::Separator::Space);
    }

    // We have multiple values, but the property claims to accept only a single one, check if it's a shorthand property.
    auto unassigned_properties = longhands_for_shorthand(property_id);
    if (unassigned_properties.is_empty())
        return ParseError::SyntaxError;

    auto stream = TokenStream { component_values };

    HashMap<UnderlyingType<PropertyID>, Vector<ValueComparingNonnullRefPtr<StyleValue const>>> assigned_values;

    while (stream.has_next_token() && !unassigned_properties.is_empty()) {
        auto property_and_value = parse_css_value_for_properties(unassigned_properties, stream);
        if (property_and_value.has_value()) {
            auto property = property_and_value->property;
            auto value = property_and_value->style_value;
            auto& values = assigned_values.ensure(to_underlying(property));
            if (values.size() + 1 == property_maximum_value_count(property)) {
                // We're done with this property, move on to the next one.
                unassigned_properties.remove_first_matching([&](auto& unassigned_property) { return unassigned_property == property; });
            }

            values.append(value.release_nonnull());
            continue;
        }

        // No property matched, so we're done.
        dbgln("No property (from {} properties) matched {}", unassigned_properties.size(), stream.peek_token().to_debug_string());
        for (auto id : unassigned_properties)
            dbgln("    {}", string_from_property_id(id));
        break;
    }

    for (auto& property : unassigned_properties)
        assigned_values.ensure(to_underlying(property)).append(property_initial_value(m_context.realm(), property));

    stream.skip_whitespace();
    if (stream.has_next_token())
        return ParseError::SyntaxError;

    Vector<PropertyID> longhand_properties;
    longhand_properties.ensure_capacity(assigned_values.size());
    for (auto& it : assigned_values)
        longhand_properties.unchecked_append(static_cast<PropertyID>(it.key));

    StyleValueVector longhand_values;
    longhand_values.ensure_capacity(assigned_values.size());
    for (auto& it : assigned_values) {
        if (it.value.size() == 1)
            longhand_values.unchecked_append(it.value.take_first());
        else
            longhand_values.unchecked_append(StyleValueList::create(move(it.value), StyleValueList::Separator::Space));
    }

    return { CompositeStyleValue::create(move(longhand_properties), move(longhand_values)) };
}

RefPtr<StyleValue> Parser::parse_css_value_for_property(PropertyID property_id, TokenStream<ComponentValue>& tokens)
{
    return parse_css_value_for_properties({ &property_id, 1 }, tokens)
        .map([](auto& it) { return it.style_value; })
        .value_or(nullptr);
}

Optional<Parser::PropertyAndValue> Parser::parse_css_value_for_properties(ReadonlySpan<PropertyID> property_ids, TokenStream<ComponentValue>& tokens)
{
    auto any_property_accepts_type = [](ReadonlySpan<PropertyID> property_ids, ValueType value_type) -> Optional<PropertyID> {
        for (auto const& property : property_ids) {
            if (property_accepts_type(property, value_type))
                return property;
        }
        return {};
    };
    auto any_property_accepts_type_percentage = [](ReadonlySpan<PropertyID> property_ids, ValueType value_type) -> Optional<PropertyID> {
        for (auto const& property : property_ids) {
            if (property_accepts_type(property, value_type) && property_accepts_type(property, ValueType::Percentage))
                return property;
        }
        return {};
    };
    auto any_property_accepts_identifier = [](ReadonlySpan<PropertyID> property_ids, ValueID identifier) -> Optional<PropertyID> {
        for (auto const& property : property_ids) {
            if (property_accepts_identifier(property, identifier))
                return property;
        }
        return {};
    };

    auto& peek_token = tokens.peek_token();

    if (auto property = any_property_accepts_type(property_ids, ValueType::EasingFunction); property.has_value()) {
        if (auto maybe_easing_function = parse_easing_value(tokens))
            return PropertyAndValue { *property, maybe_easing_function };
    }

    if (peek_token.is(Token::Type::Ident)) {
        // NOTE: We do not try to parse "CSS-wide keywords" here. https://www.w3.org/TR/css-values-4/#common-keywords
        //       These are only valid on their own, and so should be parsed directly in `parse_css_value()`.
        auto ident = value_id_from_string(peek_token.token().ident());
        if (ident.has_value()) {
            if (auto property = any_property_accepts_identifier(property_ids, ident.value()); property.has_value()) {
                (void)tokens.next_token();
                return PropertyAndValue { *property, IdentifierStyleValue::create(ident.value()) };
            }
        }

        // Custom idents
        if (auto property = any_property_accepts_type(property_ids, ValueType::CustomIdent); property.has_value()) {
            (void)tokens.next_token();
            return PropertyAndValue { *property, CustomIdentStyleValue::create(MUST(FlyString::from_utf8(peek_token.token().ident()))) };
        }
    }

    if (auto property = any_property_accepts_type(property_ids, ValueType::Color); property.has_value()) {
        if (auto maybe_color = parse_color_value(peek_token)) {
            (void)tokens.next_token();
            return PropertyAndValue { *property, maybe_color };
        }
    }

    if (auto property = any_property_accepts_type(property_ids, ValueType::Image); property.has_value()) {
        if (auto maybe_image = parse_image_value(peek_token)) {
            (void)tokens.next_token();
            return PropertyAndValue { *property, maybe_image };
        }
    }

    if (auto property = any_property_accepts_type(property_ids, ValueType::Ratio); property.has_value()) {
        if (auto maybe_ratio = parse_ratio_value(tokens))
            return PropertyAndValue { *property, maybe_ratio };
    }

    auto property_accepting_integer = any_property_accepts_type(property_ids, ValueType::Integer);
    auto property_accepting_number = any_property_accepts_type(property_ids, ValueType::Number);
    bool property_accepts_numeric = property_accepting_integer.has_value() || property_accepting_number.has_value();

    if (peek_token.is(Token::Type::Number) && property_accepts_numeric) {
        if (property_accepting_integer.has_value()) {
            auto transaction = tokens.begin_transaction();
            if (auto integer = parse_integer_value(tokens); integer && property_accepts_integer(*property_accepting_integer, integer->as_integer().integer())) {
                transaction.commit();
                return PropertyAndValue { *property_accepting_integer, integer };
            }
        }
        if (property_accepting_number.has_value()) {
            auto transaction = tokens.begin_transaction();
            if (auto number = parse_number_value(tokens); number && property_accepts_number(*property_accepting_number, number->as_number().number())) {
                transaction.commit();
                return PropertyAndValue { *property_accepting_number, number };
            }
        }
    }

    if (peek_token.is(Token::Type::Percentage)) {
        auto percentage = Percentage(peek_token.token().percentage());
        if (auto property = any_property_accepts_type(property_ids, ValueType::Percentage); property.has_value() && property_accepts_percentage(*property, percentage)) {
            (void)tokens.next_token();
            return PropertyAndValue { *property, PercentageStyleValue::create(percentage) };
        }
    }

    if (auto property = any_property_accepts_type(property_ids, ValueType::Rect); property.has_value()) {
        if (auto maybe_rect = parse_rect_value(peek_token)) {
            (void)tokens.next_token();
            return PropertyAndValue { *property, maybe_rect };
        }
    }

    if (peek_token.is(Token::Type::String)) {
        if (auto property = any_property_accepts_type(property_ids, ValueType::String); property.has_value())
            return PropertyAndValue { *property, StringStyleValue::create(MUST(String::from_utf8(tokens.next_token().token().string()))) };
    }

    if (auto property = any_property_accepts_type(property_ids, ValueType::Url); property.has_value()) {
        if (auto url = parse_url_value(peek_token)) {
            (void)tokens.next_token();
            return PropertyAndValue { *property, url };
        }
    }

    bool property_accepts_dimension = any_property_accepts_type(property_ids, ValueType::Angle).has_value()
        || any_property_accepts_type(property_ids, ValueType::Length).has_value()
        || any_property_accepts_type(property_ids, ValueType::Percentage).has_value()
        || any_property_accepts_type(property_ids, ValueType::Resolution).has_value()
        || any_property_accepts_type(property_ids, ValueType::Time).has_value();

    if (property_accepts_dimension) {
        auto transaction = tokens.begin_transaction();
        if (auto maybe_dimension = parse_dimension(peek_token); maybe_dimension.has_value()) {
            (void)tokens.next_token();
            auto dimension = maybe_dimension.release_value();
            if (dimension.is_angle()) {
                auto angle = dimension.angle();
                if (auto property = any_property_accepts_type(property_ids, ValueType::Angle); property.has_value() && property_accepts_angle(*property, angle)) {
                    transaction.commit();
                    return PropertyAndValue { *property, AngleStyleValue::create(angle) };
                }
            }
            if (dimension.is_frequency()) {
                auto frequency = dimension.frequency();
                if (auto property = any_property_accepts_type(property_ids, ValueType::Frequency); property.has_value() && property_accepts_frequency(*property, frequency)) {
                    transaction.commit();
                    return PropertyAndValue { *property, FrequencyStyleValue::create(frequency) };
                }
            }
            if (dimension.is_length()) {
                auto length = dimension.length();
                if (auto property = any_property_accepts_type(property_ids, ValueType::Length); property.has_value() && property_accepts_length(*property, length)) {
                    transaction.commit();
                    return PropertyAndValue { *property, LengthStyleValue::create(length) };
                }
            }
            if (dimension.is_resolution()) {
                auto resolution = dimension.resolution();
                if (auto property = any_property_accepts_type(property_ids, ValueType::Resolution); property.has_value() && property_accepts_resolution(*property, resolution)) {
                    transaction.commit();
                    return PropertyAndValue { *property, ResolutionStyleValue::create(resolution) };
                }
            }
            if (dimension.is_time()) {
                auto time = dimension.time();
                if (auto property = any_property_accepts_type(property_ids, ValueType::Time); property.has_value() && property_accepts_time(*property, time)) {
                    transaction.commit();
                    return PropertyAndValue { *property, TimeStyleValue::create(time) };
                }
            }
        }
    }

    // In order to not end up parsing `calc()` and other math expressions multiple times,
    // we parse it once, and then see if its resolved type matches what the property accepts.
    if (peek_token.is_function() && (property_accepts_dimension || property_accepts_numeric)) {
        if (auto maybe_calculated = parse_calculated_value(peek_token); maybe_calculated) {
            (void)tokens.next_token();
            auto& calculated = *maybe_calculated;
            // This is a bit sensitive to ordering: `<foo>` and `<percentage>` have to be checked before `<foo-percentage>`.
            if (calculated.resolves_to_percentage()) {
                if (auto property = any_property_accepts_type(property_ids, ValueType::Percentage); property.has_value())
                    return PropertyAndValue { *property, calculated };
            } else if (calculated.resolves_to_angle()) {
                if (auto property = any_property_accepts_type(property_ids, ValueType::Angle); property.has_value())
                    return PropertyAndValue { *property, calculated };
            } else if (calculated.resolves_to_angle_percentage()) {
                if (auto property = any_property_accepts_type_percentage(property_ids, ValueType::Angle); property.has_value())
                    return PropertyAndValue { *property, calculated };
            } else if (calculated.resolves_to_frequency()) {
                if (auto property = any_property_accepts_type(property_ids, ValueType::Frequency); property.has_value())
                    return PropertyAndValue { *property, calculated };
            } else if (calculated.resolves_to_frequency_percentage()) {
                if (auto property = any_property_accepts_type_percentage(property_ids, ValueType::Frequency); property.has_value())
                    return PropertyAndValue { *property, calculated };
            } else if (calculated.resolves_to_number()) {
                if (property_accepts_numeric) {
                    auto property_or_resolved = property_accepting_integer.value_or_lazy_evaluated([property_accepting_number]() { return property_accepting_number.value(); });
                    return PropertyAndValue { property_or_resolved, calculated };
                }
            } else if (calculated.resolves_to_number_percentage()) {
                if (auto property = any_property_accepts_type_percentage(property_ids, ValueType::Number); property.has_value())
                    return PropertyAndValue { *property, calculated };
            } else if (calculated.resolves_to_length()) {
                if (auto property = any_property_accepts_type(property_ids, ValueType::Length); property.has_value())
                    return PropertyAndValue { *property, calculated };
            } else if (calculated.resolves_to_length_percentage()) {
                if (auto property = any_property_accepts_type_percentage(property_ids, ValueType::Length); property.has_value())
                    return PropertyAndValue { *property, calculated };
            } else if (calculated.resolves_to_time()) {
                if (auto property = any_property_accepts_type(property_ids, ValueType::Time); property.has_value())
                    return PropertyAndValue { *property, calculated };
            } else if (calculated.resolves_to_time_percentage()) {
                if (auto property = any_property_accepts_type_percentage(property_ids, ValueType::Time); property.has_value())
                    return PropertyAndValue { *property, calculated };
            }
        }
    }

    if (auto property = any_property_accepts_type(property_ids, ValueType::Paint); property.has_value()) {
        if (auto value = parse_paint_value(tokens))
            return PropertyAndValue { *property, value.release_nonnull() };
    }

    return OptionalNone {};
}

class UnparsedCalculationNode final : public CalculationNode {
public:
    static NonnullOwnPtr<UnparsedCalculationNode> create(ComponentValue component_value)
    {
        return adopt_own(*new (nothrow) UnparsedCalculationNode(move(component_value)));
    }
    virtual ~UnparsedCalculationNode() = default;

    ComponentValue& component_value() { return m_component_value; }

    virtual String to_string() const override { VERIFY_NOT_REACHED(); }
    virtual Optional<CalculatedStyleValue::ResolvedType> resolved_type() const override { VERIFY_NOT_REACHED(); }
    virtual Optional<CSSNumericType> determine_type(Web::CSS::PropertyID) const override { VERIFY_NOT_REACHED(); }
    virtual bool contains_percentage() const override { VERIFY_NOT_REACHED(); }
    virtual CalculatedStyleValue::CalculationResult resolve(Optional<Length::ResolutionContext const&>, CalculatedStyleValue::PercentageBasis const&) const override { VERIFY_NOT_REACHED(); }
    virtual void for_each_child_node(AK::Function<void(NonnullOwnPtr<CalculationNode>&)> const&) override { }

    virtual void dump(StringBuilder& builder, int indent) const override
    {
        builder.appendff("{: >{}}UNPARSED({})\n", "", indent, m_component_value.to_debug_string());
    }

private:
    UnparsedCalculationNode(ComponentValue component_value)
        : CalculationNode(Type::Unparsed)
        , m_component_value(move(component_value))
    {
    }

    ComponentValue m_component_value;
};

// https://html.spec.whatwg.org/multipage/images.html#parsing-a-sizes-attribute
LengthOrCalculated Parser::Parser::parse_as_sizes_attribute()
{
    // 1. Let unparsed sizes list be the result of parsing a comma-separated list of component values
    //    from the value of element's sizes attribute (or the empty string, if the attribute is absent).
    auto unparsed_sizes_list = parse_a_comma_separated_list_of_component_values(m_token_stream);

    // 2. Let size be null.
    Optional<LengthOrCalculated> size;

    // 3. For each unparsed size in unparsed sizes list:
    for (auto& unparsed_size : unparsed_sizes_list) {
        // 1. Remove all consecutive <whitespace-token>s from the end of unparsed size.
        //    If unparsed size is now empty, that is a parse error; continue.
        while (!unparsed_size.is_empty() && unparsed_size.last().is_token() && unparsed_size.last().token().is(Token::Type::Whitespace))
            unparsed_size.take_last();
        if (unparsed_size.is_empty()) {
            log_parse_error();
            continue;
        }

        // 2. If the last component value in unparsed size is a valid non-negative <source-size-value>,
        //    let size be its value and remove the component value from unparsed size.
        //    FIXME: Any CSS function other than the math functions is invalid.
        //    Otherwise, there is a parse error; continue.
        if (auto source_size_value = parse_source_size_value(unparsed_size.last()); source_size_value.has_value()) {
            size = source_size_value.value();
            unparsed_size.take_last();
        } else {
            log_parse_error();
            continue;
        }

        // 3. Remove all consecutive <whitespace-token>s from the end of unparsed size.
        while (!unparsed_size.is_empty() && unparsed_size.last().is_token() && unparsed_size.last().token().is(Token::Type::Whitespace))
            unparsed_size.take_last();

        // If unparsed size is now empty, then return size.
        if (unparsed_size.is_empty())
            return size.value();

        // FIXME: If this was not the keyword auto and it was not the last item in unparsed sizes list, that is a parse error.

        // 4. Parse the remaining component values in unparsed size as a <media-condition>.
        //    If it does not parse correctly, or it does parse correctly but the <media-condition> evaluates to false, continue.
        TokenStream<ComponentValue> token_stream { unparsed_size };
        auto media_condition = parse_media_condition(token_stream, MediaCondition::AllowOr::Yes);
        if (media_condition && media_condition->evaluate(*m_context.window()) == MatchResult::True) {
            return size.value();
        } else {
            continue;
        }

        // 5. If size is not auto, then return size.
        if (size.value().is_calculated() || !size.value().value().is_auto())
            return size.value();
    }

    return Length(100, Length::Type::Vw);
}

// https://www.w3.org/TR/css-values-4/#parse-a-calculation
OwnPtr<CalculationNode> Parser::parse_a_calculation(Vector<ComponentValue> const& original_values)
{
    // 1. Discard any <whitespace-token>s from values.
    // 2. An item in values is an “operator” if it’s a <delim-token> with the value "+", "-", "*", or "/". Otherwise, it’s a “value”.
    struct Operator {
        char delim;
    };
    using Value = Variant<NonnullOwnPtr<CalculationNode>, Operator>;
    Vector<Value> values;
    for (auto& value : original_values) {
        if (value.is(Token::Type::Whitespace))
            continue;
        if (value.is(Token::Type::Delim)) {
            if (first_is_one_of(value.token().delim(), static_cast<u32>('+'), static_cast<u32>('-'), static_cast<u32>('*'), static_cast<u32>('/'))) {
                // NOTE: Sequential operators are invalid syntax.
                if (!values.is_empty() && values.last().has<Operator>())
                    return nullptr;

                values.append(Operator { static_cast<char>(value.token().delim()) });
                continue;
            }
        }

        if (value.is(Token::Type::Ident)) {
            auto maybe_constant = CalculationNode::constant_type_from_string(value.token().ident());
            if (maybe_constant.has_value()) {
                values.append({ ConstantCalculationNode::create(maybe_constant.value()) });
                continue;
            }
        }

        if (value.is(Token::Type::Number)) {
            values.append({ NumericCalculationNode::create(value.token().number()) });
            continue;
        }

        if (auto dimension = parse_dimension(value); dimension.has_value()) {
            if (dimension->is_angle())
                values.append({ NumericCalculationNode::create(dimension->angle()) });
            else if (dimension->is_frequency())
                values.append({ NumericCalculationNode::create(dimension->frequency()) });
            else if (dimension->is_length())
                values.append({ NumericCalculationNode::create(dimension->length()) });
            else if (dimension->is_percentage())
                values.append({ NumericCalculationNode::create(dimension->percentage()) });
            // FIXME: Resolutions, once calc() supports them.
            else if (dimension->is_time())
                values.append({ NumericCalculationNode::create(dimension->time()) });
            else
                VERIFY_NOT_REACHED();
            continue;
        }

        values.append({ UnparsedCalculationNode::create(value) });
    }

    // If we have no values, the syntax is invalid.
    if (values.is_empty())
        return nullptr;

    // NOTE: If the first or last value is an operator, the syntax is invalid.
    if (values.first().has<Operator>() || values.last().has<Operator>())
        return nullptr;

    // 3. Collect children into Product and Invert nodes.
    //    For every consecutive run of value items in values separated by "*" or "/" operators:
    while (true) {
        Optional<size_t> first_product_operator = values.find_first_index_if([](auto const& item) {
            return item.template has<Operator>()
                && first_is_one_of(item.template get<Operator>().delim, '*', '/');
        });

        if (!first_product_operator.has_value())
            break;

        auto start_of_run = first_product_operator.value() - 1;
        auto end_of_run = first_product_operator.value() + 1;
        for (auto i = start_of_run + 1; i < values.size(); i += 2) {
            auto& item = values[i];
            if (!item.has<Operator>()) {
                end_of_run = i - 1;
                break;
            }

            auto delim = item.get<Operator>().delim;
            if (!first_is_one_of(delim, '*', '/')) {
                end_of_run = i - 1;
                break;
            }
        }

        // 1. For each "/" operator in the run, replace its right-hand value item rhs with an Invert node containing rhs as its child.
        Vector<NonnullOwnPtr<CalculationNode>> run_values;
        run_values.append(move(values[start_of_run].get<NonnullOwnPtr<CalculationNode>>()));
        for (auto i = start_of_run + 1; i <= end_of_run; i += 2) {
            auto& operator_ = values[i].get<Operator>().delim;
            auto& rhs = values[i + 1];
            if (operator_ == '/') {
                run_values.append(InvertCalculationNode::create(move(rhs.get<NonnullOwnPtr<CalculationNode>>())));
                continue;
            }
            VERIFY(operator_ == '*');
            run_values.append(move(rhs.get<NonnullOwnPtr<CalculationNode>>()));
        }
        // 2. Replace the entire run with a Product node containing the value items of the run as its children.
        auto product_node = ProductCalculationNode::create(move(run_values));
        values.remove(start_of_run, end_of_run - start_of_run + 1);
        values.insert(start_of_run, { move(product_node) });
    }

    // 4. Collect children into Sum and Negate nodes.
    Optional<NonnullOwnPtr<CalculationNode>> single_value;
    {
        // 1. For each "-" operator item in values, replace its right-hand value item rhs with a Negate node containing rhs as its child.
        for (auto i = 0u; i < values.size(); ++i) {
            auto& maybe_minus_operator = values[i];
            if (!maybe_minus_operator.has<Operator>() || maybe_minus_operator.get<Operator>().delim != '-')
                continue;

            auto rhs_index = ++i;
            auto& rhs = values[rhs_index];

            NonnullOwnPtr<CalculationNode> negate_node = NegateCalculationNode::create(move(rhs.get<NonnullOwnPtr<CalculationNode>>()));
            values.remove(rhs_index);
            values.insert(rhs_index, move(negate_node));
        }

        // 2. If values has only one item, and it is a Product node or a parenthesized simple block, replace values with that item.
        if (values.size() == 1) {
            values.first().visit(
                [&](ComponentValue& component_value) {
                    if (component_value.is_block() && component_value.block().is_paren())
                        single_value = UnparsedCalculationNode::create(component_value);
                },
                [&](NonnullOwnPtr<CalculationNode>& node) {
                    if (node->type() == CalculationNode::Type::Product)
                        single_value = move(node);
                },
                [](auto&) {});
        }
        //    Otherwise, replace values with a Sum node containing the value items of values as its children.
        if (!single_value.has_value()) {
            values.remove_all_matching([](Value& value) { return value.has<Operator>(); });
            Vector<NonnullOwnPtr<CalculationNode>> value_items;
            value_items.ensure_capacity(values.size());
            for (auto& value : values) {
                if (value.has<Operator>())
                    continue;
                value_items.unchecked_append(move(value.get<NonnullOwnPtr<CalculationNode>>()));
            }
            single_value = SumCalculationNode::create(move(value_items));
        }
    }

    // 5. At this point values is a tree of Sum, Product, Negate, and Invert nodes, with other types of values at the leaf nodes. Process the leaf nodes.
    //     For every leaf node leaf in values:
    bool parsing_failed_for_child_node = false;
    single_value.value()->for_each_child_node([&](NonnullOwnPtr<CalculationNode>& node) {
        if (node->type() != CalculationNode::Type::Unparsed)
            return;

        auto& unparsed_node = static_cast<UnparsedCalculationNode&>(*node);
        auto& component_value = unparsed_node.component_value();

        // 1. If leaf is a parenthesized simple block, replace leaf with the result of parsing a calculation from leaf’s contents.
        if (component_value.is_block() && component_value.block().is_paren()) {
            auto leaf_calculation = parse_a_calculation(component_value.block().values());
            if (!leaf_calculation) {
                parsing_failed_for_child_node = true;
                return;
            }
            node = leaf_calculation.release_nonnull();
            return;
        }

        // 2. If leaf is a math function, replace leaf with the internal representation of that math function.
        // NOTE: All function tokens at this point should be math functions.
        else if (component_value.is_function()) {
            auto& function = component_value.function();
            auto leaf_calculation = parse_a_calc_function_node(function);
            if (!leaf_calculation) {
                parsing_failed_for_child_node = true;
                return;
            }

            node = leaf_calculation.release_nonnull();
            return;
        }

        // NOTE: If we get here, then we have an UnparsedCalculationNode that didn't get replaced with something else.
        //       So, the calc() is invalid.
        dbgln_if(CSS_PARSER_DEBUG, "Leftover UnparsedCalculationNode in calc tree! That probably means the syntax is invalid, but maybe we just didn't implement `{}` yet.", component_value.to_debug_string());
        parsing_failed_for_child_node = true;
        return;
    });

    if (parsing_failed_for_child_node)
        return nullptr;

    // FIXME: 6. Return the result of simplifying a calculation tree from values.
    return single_value.release_value();
}

bool Parser::has_ignored_vendor_prefix(StringView string)
{
    if (!string.starts_with('-'))
        return false;
    if (string.starts_with("--"sv))
        return false;
    if (string.starts_with("-libweb-"sv))
        return false;
    return true;
}

bool Parser::is_builtin(StringView name)
{
    return name.equals_ignoring_ascii_case("inherit"sv)
        || name.equals_ignoring_ascii_case("initial"sv)
        || name.equals_ignoring_ascii_case("unset"sv);
}

RefPtr<CalculatedStyleValue> Parser::parse_calculated_value(Badge<StyleComputer>, ParsingContext const& context, ComponentValue const& token)
{
    auto parser = MUST(Parser::create(context, ""sv));
    return parser.parse_calculated_value(token);
}

RefPtr<StyleValue> Parser::parse_css_value(Badge<StyleComputer>, ParsingContext const& context, PropertyID property_id, Vector<ComponentValue> const& tokens)
{
    if (tokens.is_empty() || property_id == CSS::PropertyID::Invalid || property_id == CSS::PropertyID::Custom)
        return nullptr;

    auto parser = MUST(Parser::create(context, ""sv));
    TokenStream<ComponentValue> token_stream { tokens };
    auto result = parser.parse_css_value(property_id, token_stream);
    if (result.is_error())
        return nullptr;
    return result.release_value();
}

}
