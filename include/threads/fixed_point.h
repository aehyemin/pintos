#define F (1 << 14) //fixed point 1
#define INT_MAX ((1 << 31) -1 )
#define INT_MIN (-(1 << 31))
// x and y denote fixed_point numbers in 17.14 foramt
// n is an integer
// f: 17.14로 표현한 1
int int_to_fp(int n); // integer 를 fixed point로 전환
int fp_to_int_round(int x); //fp를 int로 전환(반올림)
int fp_to_int(int x); //fp를 int로 전환(버림)
int add_fp(int x, int y); //fp의 덧셈
int add_mixed(int x, int n); //fp와 int의 덧셈
int sub_fp(int x, int y); //fp의 뺄셈 x-y
int sub_mixed(int x, int n); //fp와 int의 뺄셈 x-n
int mult_fp(int x, int y); //fp의 곱셈
int mult_mixed(int x, int n); //fp와 int의 곱셈 여기 엔이라고 썻었음
int div_fp(int x, int y); //fp의 나눗셈 x/y
int div_mixed(int x, int n); //fp와 int의 나눗셈 x/y



int int_to_fp(int n) { // integer 를 fixed point로 전환
    return n * F;
}

int fp_to_int_round(int x) {//fp를 int로 전환(반올림)
    if (x >= 0){
        return (x + F / 2)/ F;
    } else {
        return (x - F / 2) / F;
    }
}

int fp_to_int(int x) { //fp를 int로 전환(버림)
    return x / F;
}

int add_fp(int x, int y) { //fp의 덧셈
    return x + y;
}

int add_mixed(int x, int n) { //fp와 int의 덧셈
    return x + n * F;
}

int sub_fp(int x, int y) {//fp의 뺄셈 x-y
    return x - y;
}

int sub_mixed(int x, int n) { //fp와 int의 뺄셈 x-n
    return x - n * F;
}

int mult_fp(int x, int y) {//fp의 곱셉 
    return ((int64_t) x) * y / F;
}

int mult_mixed(int x, int n) {//fp와 int의 곱셉
    return x * n;
}

int div_fp(int x, int y) {//fp의 나눗셈 x/y 
    return ((int64_t) x) * F / y;
}

int div_mixed(int x, int n) {//fp와 int의 나눗셈 x/y
    return x / n;
}