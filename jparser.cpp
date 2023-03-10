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

constexpr std::size_t CashLine = 64;

void* AllocAligned(std::size_t size, int align) {
	return _aligned_malloc(size, align);
}

void FreeAligned(void* ptr) {
	_aligned_free(ptr);
}

template <typename T>
T* AllocAligned(std::size_t n)
{
	return (T*)(AllocAligned(sizeof(T) * n, 64));
}

// A region-based memory manager "Fast allocation and deallocation of memory based on object lifetimes"
template <int nCashLine = 64>
class DataArena
{
	const size_t m_blockSize;
	size_t m_currentBlockPos;
	size_t m_currentAllocBlockSize;
	uint8_t* m_currentBlock;
	size_t m_fragmentSize;
	//std::vector<int> m_pos;
	std::list<std::pair<uint8_t*, int>> m_used;
	std::list<std::pair<uint8_t*, int>> m_available;

public:
	explicit DataArena(size_t size = 1024 * 1024) :
		m_blockSize(size),
		m_currentAllocBlockSize(0),
		m_currentBlock(nullptr),
		m_currentBlockPos(0),
		m_fragmentSize(0)
	{
		// Default block is 1MB
	}

	DataArena(const DataArena& arena) = delete;
	DataArena& operator=(const DataArena& arena) = delete;

	DataArena(DataArena&& arena) noexcept :
		m_blockSize(arena.m_blockSize), m_currentBlockPos(arena.m_currentBlockPos), m_currentAllocBlockSize(arena.m_currentAllocBlockSize), m_currentBlock(arena.m_currentBlock), m_fragmentSize(arena.m_fragmentSize), m_used(std::move(arena.m_used)), m_available(std::move(arena.m_available))
	{
		arena.m_currentBlock = nullptr;
	}

	DataArena& operator=(DataArena&& arena) noexcept
	{
		Release();	// Release memory
		m_blockSize = arena.m_blockSize;
		m_currentBlockPos = arena.m_currentBlockPos;
		m_currentAllocBlockSize = arena.m_currentAllocBlockSize;
		m_currentBlock = arena.m_currentBlock;
		arena.m_currentBlock = nullptr;
		m_fragmentSize = arena.m_fragmentSize;
		m_used = std::move(arena.m_used);
		m_available = std::move(arena.m_available);
		return *this;
	}

	void* Alloc(size_t bytes)
	{
		const auto align = alignof(std::max_align_t);
		bytes = (bytes + align - 1) & ~(align - 1);	 // Find a proper size to match the aligned boundary
		if (m_currentBlockPos + bytes > m_currentAllocBlockSize) {
			// Put into used list. A fragment generates.
			if (m_currentBlock) {
				m_used.push_back(std::make_pair(m_currentBlock, m_currentAllocBlockSize));
				m_fragmentSize += m_currentAllocBlockSize - m_currentBlockPos;
				m_currentBlock = nullptr;
				m_currentBlockPos = 0;
				//std::cout << "Current block can not accommodate this size, put it into used list.\n";
			}

			// Try to find available block
			for (auto it = m_available.begin(); it != m_available.end(); ++it) {
				if (bytes <= it->second) {
					m_currentBlock = it->first;
					m_currentAllocBlockSize = it->second;
					m_currentBlockPos = 0;
					break;
				}
			}

			if (!m_currentBlock) {
				// Available space can not be found. Allocates new memory
				m_currentAllocBlockSize = (std::max)(bytes, m_blockSize);
				m_currentBlock = static_cast<uint8_t*>(AllocAligned(m_currentAllocBlockSize, nCashLine));
				if (m_currentBlock == nullptr) {
					return nullptr;
				}
				m_currentBlockPos = 0;
			}
			m_currentBlockPos = 0;
		}
		const auto ptr = m_currentBlock + m_currentBlockPos;
		m_currentBlockPos += bytes;
		return ptr;
	}

	/**
	 * \brief Allocate for \a n objects for type \a T, runs constructor depends on \a construct on it and return its pointer
	 *
	 * \note For safety, this function should be check if the \a T is a type of POD or trivial.
	 *		 Maybe it can be checked by \a std::is_trivial or \a std::is_pod(deprecated).
	 *		 This issued will be addressed later.
	 *
	 * \sa Reset()
	 */
	template <typename T>
	T* Alloc(size_t n, bool construct = true)
	{
		const auto ptr = static_cast<T*>(Alloc(n * sizeof(T)));
		if (ptr == nullptr)
			return nullptr;
		if (construct)
			for (auto i = 0; i < n; i++)
				new (&ptr[i]) T();
		return ptr;
	}

	template <typename T, typename... Args>
	T* AllocConstruct(Args &&... args)
	{
		const auto ptr = static_cast<T*>(Alloc(sizeof(T)));
		if (ptr == nullptr) return nullptr;
		new (ptr) T(std::forward<Args>(args)...);
		return ptr;
	}

	void Release()
	{
		for (auto it = m_used.begin(); it != m_used.end(); ++it) FreeAligned(it->first);
		for (auto it = m_available.begin(); it != m_available.end(); ++it) FreeAligned(it->first);
		//FreeAligned(m_currentBlock);
	}

	void Shrink()
	{
		for (auto it = m_available.begin(); it != m_available.end(); ++it) FreeAligned(it->first);
	}

	void Reset()
	{
		m_currentBlockPos = 0;
		m_fragmentSize = 0;
		m_available.splice(m_available.begin(), m_used);
	}

	size_t TotalAllocated() const
	{
		auto alloc = m_currentAllocBlockSize;
		for (auto it = m_used.begin(); it != m_used.end(); ++it) alloc += it->second;
		for (auto it = m_available.begin(); it != m_available.end(); ++it) alloc += it->second;
		return alloc;
	}

	size_t FragmentSize() const
	{
		return m_fragmentSize;
	}

	double FragmentRate() const
	{
		return static_cast<double>(m_fragmentSize) / TotalAllocated();
	}

	~DataArena()
	{
		Release();
	}
};

using Arena64 = DataArena<64>;

Arena64 g_arena(1024 * 1024 * 1024);

template<class T>
struct ArenaAllocator
{
	typedef T value_type;

	// Arena64* m_arena = nullptr;
	ArenaAllocator() = default;

	template<class U>
	constexpr ArenaAllocator(const ArenaAllocator <U>&) noexcept {}

	T* allocate(std::size_t n)
	{
		return (T*)g_arena.Alloc(n * sizeof(T));
		//return (T*)std::malloc(n);
	}

	void deallocate(T* p, std::size_t n) noexcept
	{
		//return std::free(p);
		//std::cout << "dealloate: " << n << std::endl;;
	}
};

template<class T, class U>
inline bool operator==(const ArenaAllocator <T>&, const ArenaAllocator <U>&) {
	return true;
}

template<class T, class U>
inline bool operator!=(const ArenaAllocator <T>&, const ArenaAllocator <U>&) {
	return false;
}


enum class object_type {
	dict,
	array,
	text,
	number,
	null,
	boolean
};

struct job;
// using JsonString = std::string;// std::basic_string<char, std::char_traits<char>, ArenaAllocator<char>>;;
using JsonString = std::string_view;// std::basic_string<char, std::char_traits<char>, ArenaAllocator<char>>;;
//using JsonArray = std::vector<job, ArenaAllocator<job>>;
using JsonArray = std::vector<job>;
//using JsonArray = std::list<job, ArenaAllocator<job>>;
using JsonDict = std::map<
	JsonString,
	job,
	std::less<JsonString>,
	ArenaAllocator<std::pair<const JsonString, job>>>;
// using JsonDict = std::unordered_map<JsonString, job, std::hash<JsonString>, std::equal_to<JsonString>, ArenaAllocator<std::pair<const JsonString, job>>>;
// using JsonDict = std::unordered_map<JsonString, job>;
// using JsonDict = std::map<JsonString, job>;
using JsonNumber = double;
using JsonBoolean = bool;
struct JsonNull {};


// perfermance rank:
/*

list/treemap with a good allocator

vector/treemap with good allocator

list/treemap

vector with allocator / treemap

vector/treemap

vector/hashmap

*/

template<class... Ts> struct overloaded : Ts... { using Ts::operator()...; };
template<class... Ts> overloaded(Ts...)->overloaded<Ts...>;

struct job {
	std::variant<JsonNumber, JsonBoolean, JsonString, JsonDict, JsonArray, JsonNull> value;
	job() : value(JsonNull()) {}
	job(JsonNumber n) : value(n) {}
	job(JsonBoolean v) : value(v) {}
	job(JsonString text) : value(text) {}
	job(JsonDict dict) : value(std::move(dict)) {}
	job(JsonArray array) : value(std::move(array)) {}

	job(job&&)noexcept = default;
	job& operator=(job&&)noexcept = default;

	//job(job& other){
	//}
	//job& operator=(const job& other) {
	//	return *this;
	//}

	job& operator[](const JsonString& key) {
		return std::visit([&](auto&& arg)->job& {
			using T = std::decay_t<decltype(arg)>;
			if constexpr (std::is_same_v<T, JsonDict>) {
				return arg[key];
			}
			}, value);
	}

	template<typename T>
	T as() {
		if (auto* ptr = std::get_if<T>(&value)) {
			return*ptr;
		}
		throw std::runtime_error("value is invalid");
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
				for (const auto& e : *array) {
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
				for (auto& e : *dict) {
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
		else if (auto* boolean = std::get_if<JsonBoolean>(&value)) {
			os << *boolean ? "true" : "false";
		}
		else if (auto* number = std::get_if<JsonNumber>(&value)) {
			os << *number;
		}
		else if (auto* text = std::get_if<JsonString>(&value)) {
			os << '"' << *text << '"';
			//os << '"';
			//for (auto c : *text) {
			//	if (c == '"') {
			//		os << "\\\"";
			//	}
			//	else {
			//		os << c;
			//	}
			//}
			//os << '"';
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
				dict.emplace(std::move(key), std::move(value));
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
		// array.reserve(50);
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

	JsonString parse_string() {
		expect('"');
		auto begin = pos;
		//JsonString text;
		while (j[pos] != '"') {
			if (j[pos] != '\\') {
				pos++;
			}
			else {
				// escape char
				// text.append(&j[begin], &j[pos]);
				if (char c = next2()) {
					// text.push_back(c);
				}
				else {
					throw std::runtime_error("\\0 encountered.");
				}
				pos++;
				begin = pos;
			}
		}
		// text.append(&j[begin], &j[pos]);
		JsonString text(&j[begin], pos - begin);
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
		"data\\test1.json",
		"data\\test2.json",
		"data\\Boing.ahap"
		"data\\Drums.ahap",
		"data\\Gravel.ahap",
		"data\\Heartbeats.ahap",
		"data\\Inflate.ahap",
		"data\\Oscillate.ahap",
		"data\\Rumble.ahap"
		"data\\canada.json",
		"data\\citm_catelog.json",
		"data\\twitter.json",
		"data\\pass01.json",
		"data\\pass02.json",
		"data\\pass03.json",
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
					std::stringstream ss1;
					job.pretty_print(ss1);
					ankerl::nanobench::doNotOptimizeAway(job);
					json_parser jp2;
					ss1 >> jp2;
					auto job2 = jp2.parse();
					std::stringstream ss2;
					job2.pretty_print(ss2);
					g_arena.Reset();
					}
				);
			}
			catch (std::exception& e) {
				std::cout << each << " exeption: " << e.what() << std::endl;
			}

		}
	}
	g_arena.Release();
}
