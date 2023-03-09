// jparser.cpp : This file contains the 'main' function. Program execution begins and ends there.
//

#include <iostream>
#include <vector>
#include <unordered_map>
#include <fstream>
#include <sstream>
#include <string>

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
using JsonDict = std::unordered_map<std::string, job>;

struct job {
	std::vector<job> array;
	std::unordered_map<std::string, job> dict;
	double number;
	bool boolean;
	std::string text;
	object_type type;
	job() :type(object_type::null), number(0), boolean(false) {}
	job(double n) :number(n), type(object_type::number) {}
	job(bool v) :boolean(v), type(object_type::boolean) {}
	job(std::string text) :text(std::move(text)), type(object_type::text) {}
	job(std::unordered_map<std::string, job> dict) :dict(std::move(dict)), type(object_type::dict) {}
	job(std::vector<job> array) :array(std::move(array)), type(object_type::array) {}

	job& operator[](const std::string& key) {
		if (type != object_type::dict) {
			throw std::runtime_error("not a dict");
		}
		return dict[key];
	}

	job& operator[](const char* key) {
		if (type != object_type::dict) {
			throw std::runtime_error("not a dict");
		}
		return dict[key];
	}

	std::ofstream& operator<<(std::ofstream& os) {
		return os;
	}

	template<typename T>
	T as() {
		if constexpr (std::is_same_v<T, double>) {
			if (type == object_type::number) {
				return number;
			}
			throw std::runtime_error("type error\n");
		}
		if constexpr (std::is_same_v<T, std::string>) {
			if (type == object_type::text) {
				return text;
			}
			throw std::runtime_error("type error\n");
		}
		if constexpr (std::is_same_v<T, bool>) {
			if (type == object_type::boolean) {
				return boolean;
			}
			throw std::runtime_error("type error\n");
		}
		if constexpr (std::is_same_v<T, JsonArray>) {
			if (type == object_type::array) {
				return array;
			}
			throw std::runtime_error("type error\n");
		}

		if constexpr (std::is_same_v<T, JsonDict>) {
			if (type == object_type::dict) {
				return dict;
			}
			throw std::runtime_error("type error\n");
		}
	}

	bool is_null() {
		return type == object_type::null;
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
		if (type == object_type::array) {
			if (array.empty() == true) {
				os << "[]";
			}
			else {
				os << "[\n";
				_print_indent(indent + 1, os);
				int count = 0;
				for (auto e : array) {
					e._print(indent + 1, os);
					count++;
					if (count != array.size()) {
						os << ",\n";
						_print_indent(indent + 1, os);
					}
					else {
						os << "\n";
						_print_indent(indent, os);

					}
				}
				//_print_indent(indent + 1);
				os << "]";
			}
		}
		else if (type == object_type::dict) {
			// _print_indent(indent);
			if (dict.empty() == true) {
				os << "{}";
			}
			else {
				os << "{\n";
				_print_indent(indent + 1, os);
				auto count = 0;
				for (auto e : dict) {
					os << "\"" << e.first << "\": ";
					e.second._print(indent + 1, os);

					count++;
					if (count != dict.size()) {
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
		else if (type == object_type::boolean) {
			os << boolean ? "true" : "false";
		}
		else if (type == object_type::number) {
			os << number;
		}
		else if (type == object_type::text) {
			os << '"' << text << '"';
		}
		else if (type == object_type::null) {
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
		parse_whitespace();
		if (peek() == '{') {
			auto ret = job(parse_dict());
			return ret;
		}
		else if (peek() == '[') {
			auto ret = job(parse_array());
			return ret;
		}
		throw std::runtime_error("parse error\n");
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
		//if (expect('null')) {
		//	return job();
		//}
		if (next() == 'u' && next() == 'l' && next() == 'l') {
			next();
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
			parse_whitespace();
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

	std::unordered_map<std::string, job> parse_dict() {
		expect('{');
		std::unordered_map<std::string, job> dict;
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
			if (peek() != ',') {
				expect('}');
				return dict;
			}
			next(); // ,
		}
		expect('}');
		return dict;
	}

	std::vector<job> parse_array() {
		expect('[');
		std::vector<job> array;
		while (peek() != ']') {
			auto val = parse_value();
			array.emplace_back(val);
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

	std::string parse_string() {
		parse_whitespace();
		expect('"');
		auto begin = pos;
		while (j[pos] != '"')pos++;
		auto key = std::string(&j[begin], &j[pos]);
		expect('"');
		return key;
	}

	job parse_value() {
		parse_whitespace();
		if (peek() == 'n') { // null
			return parse_null();
		}
		else if (peek() == 'f' || peek() == 't') {  // boolean
			return parse_boolean();
		}
		else if (peek() == '"') { // text
			return parse_string();
		}
		else if (_is_digit(peek())) { // number
			return parse_number();
		}
		else if (peek() == '{') {
			return parse_dict();
		}
		else if (peek() == '[') {
			return parse_array();
		}
		throw std::runtime_error("parse value error\n");
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
		parse_whitespace();
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
	};

	for (const auto& each : filenames) {
		std::ifstream ifs(each, std::ios::in);
		if (ifs.is_open() == true) {
			json_parser jp;
			ifs >> jp;
			auto job = jp.parse();
			std::stringstream ss1;
			job.pretty_print(ss1);

			json_parser jp2;
			ss1 >> jp2;
			auto job2 = jp2.parse();
			std::stringstream ss2;
			job2.pretty_print(std::cout);
		}
	}
}
