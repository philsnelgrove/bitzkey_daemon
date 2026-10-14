#include "core/Exchange.hpp"

#include <cctype>
#include <sstream>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "core/PyFormat.hpp"
#include "core/Simulation.hpp"

namespace bitzkey {

using nlohmann::json;
using nlohmann::ordered_json;

namespace {

template <class Json>
void write(std::string& out, const Json& value) {
    switch (value.type()) {
        case nlohmann::json::value_t::object: {
            out += '{';
            bool first = true;
            for (auto it = value.begin(); it != value.end(); ++it) {
                if (!first) out += ", ";
                first = false;
                out += Json(it.key()).dump();
                out += ": ";
                write(out, it.value());
            }
            out += '}';
            break;
        }
        case nlohmann::json::value_t::array: {
            out += '[';
            bool first = true;
            for (const auto& item : value) {
                if (!first) out += ", ";
                first = false;
                write(out, item);
            }
            out += ']';
            break;
        }
        case nlohmann::json::value_t::number_float:
            out += py_float_repr(value.template get<double>());
            break;
        case nlohmann::json::value_t::boolean:
            out += value.template get<bool>() ? "true" : "false";
            break;
        default:
            out += value.dump(-1, ' ', true);  // strings (ASCII-escaped, as Python does), integers, null
            break;
    }
}

std::string python_dumps(const json& value) {
    std::string out;
    write(out, value);
    return out;
}

PartialMind mind_from_record(const json& value, int version) {
    if (!value.is_object()) throw std::invalid_argument("\"mind\" must map trait names to numbers.");
    const double scale = version == 1 ? static_cast<double>(TRAIT_MAX) / V1_MAX : 1.0;
    PartialMind mind;
    for (int trait = 0; trait < TEMPERAMENT_COUNT; ++trait) {
        const std::string name(TEMPERAMENT[trait]);
        if (!value.contains(name)) continue;
        const auto& item = value[name];
        double number;
        if (item.is_number() || item.is_boolean()) {
            number = item.is_boolean() ? (item.get<bool>() ? 1.0 : 0.0) : item.get<double>();
        } else {
            try {
                std::size_t used = 0;
                const std::string text = item.is_string() ? item.get<std::string>() : std::string("x");
                number = std::stod(text, &used);
                if (used != text.size()) throw std::invalid_argument("trailing text");
            } catch (const std::exception&) {
                throw std::invalid_argument("\"mind\" trait \"" + name + "\" must be a number, not " + python_dumps(item) + ".");
            }
        }
        mind.traits[trait] = std::max(0.0, std::min(static_cast<double>(TRAIT_MAX), number * scale));
    }
    return mind;
}

std::string text_of(const json& value) { return value.is_string() ? value.get<std::string>() : value.dump(); }

void bitzkeys_in(const json& value, std::vector<Imported>& found) {
    if (value.is_array()) {
        for (const auto& element : value) bitzkeys_in(element, found);
        return;
    }
    if (value.is_object() && value.contains("bitzkeys")) {
        bitzkeys_in(value["bitzkeys"], found);
        return;
    }
    if (value.is_object() && value.contains("ring") && value.contains("dot")) {
        Imported imported;
        imported.dna = genome_from_record(value);
        try {
            if (value.contains("brain") && !value["brain"].is_null()) {
                imported.brain = std::make_shared<const Brain>(Brain::from_record(value["brain"]));
            }
            if (value.contains("mind") && !value["mind"].is_null()) {
                imported.mind = mind_from_record(value["mind"], record_version(value));
            }
        } catch (const std::invalid_argument& error) {
            throw std::invalid_argument("Bitzkey " + text_of(value["ring"]) + "/" + text_of(value["dot"]) + ": " + error.what());
        }
        found.push_back(std::move(imported));
        return;
    }
    throw std::invalid_argument("Expected a Bitzkey with \"ring\" and \"dot\", found: " + python_dumps(value).substr(0, 60));
}

}  // namespace

std::string dumps(const ordered_json& value) {
    std::string out;
    write(out, value);
    return out;
}

ordered_json genome_record(Dna dna) {
    ordered_json record = ordered_json::object();
    record["version"] = GENOME_VERSION;
    record["ring"] = hex_word(body_word(dna));
    record["dot"] = hex_word(mind_word(dna));
    record["social"] = hex_word(social_word(dna));
    return record;
}

ordered_json bitzkey_record(const Bitzkey& agent) {
    ordered_json record = genome_record(agent.dna);
    if (agent.species != Species::Plant) {
        ordered_json mind = ordered_json::object();
        for (int trait = 0; trait < TEMPERAMENT_COUNT; ++trait) {
            mind[std::string(TEMPERAMENT[trait])] = py_round(agent.mind[trait], MIND_DIGITS);
        }
        record["mind"] = std::move(mind);
    }
    if (agent.brain) record["brain"] = agent.brain->to_record();
    return record;
}

std::string format_records(std::span<const ordered_json> records) {
    if (records.size() == 1) return dumps(records[0]);
    std::string text = "[\n";
    for (std::size_t index = 0; index < records.size(); ++index) {
        text += "  " + dumps(records[index]);
        text += index + 1 < records.size() ? ",\n" : "\n";
    }
    return text + "]";
}

std::string export_bitzkeys(std::span<const Bitzkey* const> agents) {
    std::vector<ordered_json> records;
    for (const Bitzkey* agent : agents) records.push_back(bitzkey_record(*agent));
    return format_records(records);
}

std::vector<Imported> import_bitzkeys(std::string_view text) {
    std::vector<Imported> found;
    std::size_t position = 0;
    while (true) {
        while (position < text.size() &&
               (std::isspace(static_cast<unsigned char>(text[position])) || text[position] == ',')) {
            ++position;
        }
        if (position == text.size()) return found;
        // Read one JSON value from here, leaving whatever follows it.
        std::istringstream stream{std::string(text.substr(position))};
        json value;
        try {
            stream >> value;
        } catch (const json::parse_error& error) {
            const std::size_t at = position + (error.byte > 0 ? error.byte - 1 : 0);
            std::size_t line = 1, column = 1;
            for (std::size_t index = 0; index < at && index < text.size(); ++index) {
                if (text[index] == '\n') {
                    ++line;
                    column = 1;
                } else {
                    ++column;
                }
            }
            throw std::invalid_argument("Not valid JSON at line " + std::to_string(line) + ", column " +
                                        std::to_string(column) + ".");
        }
        const auto consumed = stream.tellg();
        position = consumed < 0 ? text.size() : position + static_cast<std::size_t>(consumed);
        bitzkeys_in(value, found);
    }
}

}  // namespace bitzkey
