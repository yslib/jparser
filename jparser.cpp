// jparser.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <string>
#include <map>
#include <list>
#include <variant>

#define ANKERL_NANOBENCH_IMPLEMENT
#include "nanobench.h"

enum class object_type {
	dict,
	array,
	text,
	number,
	null,
	boolean
};

struct job;
using JsonArray = std::vector<job>;
using JsonDict = std::map<std::string, job>;
struct JsonNull {};

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...)->overloaded<Ts...>;


struct job {
	std::variant<double, bool, std::string, JsonDict, JsonArray, JsonNull> value;
	job() :value(JsonNull()) {}
	job(double n) :value(n) {}
	job(bool v) : value(v) {}
	job(std::string text) :value(text) {}
	job(JsonDict dict) :value(std::move(dict)) {}
	job(JsonArray array) :value(std::move(array)) {}

	job& operator[](const std::string& key) {
		return std::visit([&](auto&& arg)->job& {
			using T = std::decay_t<decltype(arg)>;
			if constexpr (std::is_same_v<T, JsonDict>) {
				return arg[key];
			}
			}, value);
	}

	std::ofstream& operator<<(std::ofstream& os) {
		return os;
	}

	template<typename T>
	T as() {
		if (auto* ptr = std::get_if<T>(&value)) {
			return*ptr;
		}
		throw std::runtime_error("value is invalid");
	}

	void reset(size_t pos) {
		pos = pos;
	}

	void pretty_print(std::ostream& os)const {
		int indent = 0;
		_print(indent, os);
		os << std::endl;
	}

	void _print_indent(int n, std::ostream& os)const {
		while (n--) {
			os << "\t";
		}
	}
	void _print(int indent, std::ostream& os)const {
		if (auto* array = std::get_if<JsonArray>(&value)) {
			if (array->empty() == true) {
				os << "[]";
			}
			else {
				os << "[\n";
				_print_indent(indent + 1, os);
				int count = 0;
				for (auto e : *array) {
					e._print(indent + 1, os);
					count++;
					if (count != array->size()) {
						os << ",\n";
						_print_indent(indent + 1, os);
					}
					else {
						os << "\n";
						_print_indent(indent, os);

					}
				}
				os << "]";
			}
		}
		else if (auto* dict = std::get_if<JsonDict>(&value)) {
			if (dict->empty() == true) {
				os << "{}";
			}
			else {
				os << "{\n";
				_print_indent(indent + 1, os);
				auto count = 0;
				for (auto e : *dict) {
					os << "\"" << e.first << "\": ";
					e.second._print(indent + 1, os);
					count++;
					if (count != dict->size()) {
						os << ",\n";
						_print_indent(indent + 1, os);
					}
					else {
						os << "\n";
						_print_indent(indent, os);
					}
				}
				os << "}";
			}
		}
		else if (auto* boolean = std::get_if<bool>(&value)) {
			os << *boolean ? "true" : "false";
		}
		else if (auto* number = std::get_if<double>(&value)) {
			os << *number;
		}
		else if (auto* text = std::get_if<std::string>(&value)) {
			os << '"';
			for (auto c : *text) {
				if (c == '"') {
					os << "\\\"";
				}
				else {
					os << c;
				}
			}
			os << '"';
		}
		else if (auto* null = std::get_if<JsonNull>(&value)) {
			os << "null";
		}
	}
};

struct json_parser {
	size_t pos;
	std::string j;
	json_parser() :pos(0) {}
	json_parser(std::string json) :pos(0), j(json) {}

	job parse() {
		pos = 0;
		return parse_value();
	}

private:
	void parse_whitespace() {
		while (j[pos] == '\n' ||
			j[pos] == '\t' ||
			j[pos] == '\r' ||
			j[pos] == ' ')
			pos++;
	}

	char next() {
		parse_whitespace();
		if (pos >= j.length())return '\0';
		pos++;
		return j[pos];
	}

	job parse_null() {
		parse_whitespace();
		if (expect("null")) {
			return job();
		}
		throw std::runtime_error("null error");
	}

	bool expect(const char* s) {
		parse_whitespace();
		for (auto p = s; *p; p++) {
			if (*p != j[pos++]) {
				return false;
			}
		}
		return true;
	}

	bool expect(char e) {
		parse_whitespace();
		if (j[pos] == e) {
			pos++;
			return true;
		}
		throw std::runtime_error("expect error");
	}

	char peek() {
		parse_whitespace();
		return j[pos];
	}

	char* peek_ptr() {
		parse_whitespace();
		return &j[pos];
	}

	JsonDict parse_dict() {
		expect('{');
		JsonDict dict;
		//dict.reserve(50);
		while (peek() != '}') {
			auto key = parse_string();
			expect(':');
			auto value = parse_value();
			if (dict.find(key) == dict.end()) {
				dict[key] = value;
			}
			else {
				throw std::runtime_error("duplicate key");
			}
			if (peek() == ',') {
				next();
			}
			else {
				expect('}');
				return dict;
			}
		}
		expect('}');
		return dict;
	}

	JsonArray parse_array() {
		expect('[');
		JsonArray array;
		array.reserve(50);
		while (peek() != ']') {
			auto val = parse_value();
			array.push_back(std::move(val));
			if (peek() != ',') {
				expect(']');
				return array;
			}
			else {
				next(); //,
			}
		}
		expect(']');
		return array;
	}

	char next2() {
		if (pos >= j.length())return '\0';
		pos++;
		return j[pos];
	}

	std::string parse_string() {
		expect('"');
		auto begin = pos;
		std::string text;
		while (j[pos] != '"') {
			if (j[pos] != '\\') {
				pos++;
			}
			else {
				// escape char
				text.append(&j[begin], &j[pos]);
				if (char c = next2()) {
					text.push_back(c);
				}
				else {
					throw std::runtime_error("\\0 encountered.");
				}
				pos++;
				begin = pos;
			}
		}
		text.append(&j[begin], &j[pos]);
		expect('"');
		return text;
	}

	job parse_value() {
		switch (peek()) {
		case 'n':return parse_null();
		case 'f':
		case 't':return parse_boolean();
		case '"':return parse_string();
		case '{':return parse_dict();
		case '[':return parse_array();
		default:
		{
			if (_is_digit(peek())) {
				return parse_number();
			}
			throw std::runtime_error("parse value error\n");
		}
		}
	}

	job parse_number() {
		auto p = peek_ptr();
		char* end;
		auto ret = std::strtod(p, &end);
		if (end == p) {
			throw std::runtime_error("parse number error\n");
			return job();
		}
		else {
			pos += (end - p);
			return job(ret);
		}
	}

	bool _is_digit(char c) {
		return std::isdigit(c) || c == '+' || c == '-';
	}

	job parse_boolean() {
		if (peek() == 't') {  // true
			if (expect("true")) {
				return job(true);
			}
		}
		else if (peek() == 'f') {  // false
			if (expect("false")) {
				return job(false);
			}
		}
		throw std::runtime_error("parse boolean error");
	}

};

std::istream& operator>>(std::istream& ifs, json_parser& jp) {
	jp = json_parser(std::string{ std::istreambuf_iterator<char>{ ifs }, std::istreambuf_iterator<char>{} });
	return ifs;
}


int main()
{
	std::vector<std::string> filenames = {
		"test1.json",
		"test2.json",
		"Boing.ahap"
		"Drums.ahap",
		"Gravel.ahap",
		"Heartbeats.ahap",
		"Inflate.ahap",
		"Oscillate.ahap",
		"Rumble.ahap"
		"canada.json",
		"citm_catelog.json",
		"twitter.json",
		"pass01.json",
		"pass02.json",
		"pass03.json",
	};



	for (const auto& each : filenames) {
		std::ifstream ifs(each, std::ios::in);
		if (ifs.is_open() == true) {
			json_parser jp;
			ifs >> jp;
			try {
				ankerl::nanobench::Bench().minEpochIterations(200).run(each, [&] {
					//jp.j = R"("\\")";
					auto job = jp.parse();
					// job.pretty_print(std::cout);
					ankerl::nanobench::doNotOptimizeAway(job);
					//json_parser jp2;
					//ss1 >> jp2;
					//auto job2 = jp2.parse();
					//std::stringstream ss2;
					//job2.pretty_print(std::cout);
					//std::cout << each << " passed.\n";
					}
				);
			}
			catch (std::exception& e) {
				std::cout << each << " exeption: " << e.what() << std::endl;
			}

		}
	}
}
