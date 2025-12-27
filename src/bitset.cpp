#include "bitset.h"


Bitset::Bitset(std_bs&& base):std_bs(base){}

Bitset::Bitset(Bitset::bitlen_t bit):std_bs(){
	set(bit);
}

Bitset::Bitset(std::initializer_list<bitlen_t> bits):std_bs(){
	for(auto i = bits.begin(); i!= bits.end(); ++i){
		set(*i);
	}
}

Bitset::Bitset(std::vector<bitlen_t> list){
	for(auto i: list){
		set(i);
	}
}

Bitset::bitlen_t Bitset::count() const{
	return static_cast<Bitset::bitlen_t>(std_bs::count());
}

Bitset::bitlen_t Bitset::first() const{
	// 跨平台实现：替代 GCC 特定的 _Find_first()
	for(bitlen_t i = 0; i < size(); ++i) {
		if(test(i)) return i;
	}
	return size();
}

Bitset::bitlen_t Bitset::next(Bitset::bitlen_t bit) const{
	// 跨平台实现：替代 GCC 特定的 _Find_next()
	for(bitlen_t i = bit + 1; i < size(); ++i) {
		if(test(i)) return i;
	}
	return size();
}

bool Bitset::contains(Bitset::bitlen_t bit) const{
	return std_bs::test(bit);
}

void Bitset::set(Bitset::bitlen_t bit){
	std_bs::set(bit);
}

void Bitset::reset(Bitset::bitlen_t bit){
	std_bs::reset(bit);
}

void Bitset::clear(){
	std_bs::reset();
}

Bitset::bitlen_t Bitset::size() const{
	return static_cast<Bitset::bitlen_t>(std_bs::size());
}

Bitset& Bitset::operator|=(const Bitset& other){
	std_bs::operator|=(other);
	return *this;
}

bool Bitset::operator==(const Bitset& other) const{
	return std_bs::operator==(other);
}

/*
Bitset& Bitset::operator|=(Bitset::bitlen_t other){
	set(other);
	return *this;
}
*/

Bitset operator|(const Bitset& lhs, const Bitset& rhs){
	return static_cast<const Bitset::std_bs&>(lhs) | static_cast<const Bitset::std_bs&>(rhs);
}

std::ostream& operator<<(std::ostream& out, const Bitset& set){
	if(set.count() == 0) return out << "()";
	Bitset::bitlen_t bit = set.first();
	out << '(' << bit;
	if(set.count() == 1) return out << ",)";
	while((bit = set.next(bit)) != set.size()){
		out << ',' << bit;
	}
	return out << ')';
}
