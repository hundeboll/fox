/*
 * Compile with:
 *   g++ -std=c++11 budgets.cpp -o budgets
 *
 * And run it with four arguments:
 *   ./budgets <g> <e1> <e2> <e3>
 */

#include <iostream>
#include <limits>
#include <stdlib.h>
#include <math.h>

#define ONE 255

double peyman_r(double g, float e1, float e2, float e3)
{
    double l;
    if ((1-e2) < (1-e1)*(e3))
        l=((1)/((1-e1)*(e3))) ;
    else 
        l=((-g*(-1+e2+e3-(e1*e3)))/((2-e3-e2)*(1-e1)*e3 -(1-e3)*(-1+e2+e3-e1*e3)));

    return l;
}

float peyman_source(double g, float e1, float e2, float e3)
{
    float l;
    if ((1-e2) < (1-e1)*(e3))
        l=((1)/((1-e1)*(e3)));
    else
        l=((-g*(-1+e2+e3-(e1*e3)))/((2-e3-e2)*(1-e1)*e3 -(1-e3)*(-1+e2+e3-e1*e3)));

    double d_r=l-l*e3;
    float nom = ((g - (l-l*e3)));
    float denom = (2-(e2+e3));
    float budget=l +(nom/denom);

    return budget;
}

float peyman_helper(double g, float e1, float e2, float e3)
{
    float r;

    if ((1-e2) < (1-e1)*(e3))
        r = ((1)/((1-e1)*(e3)));
    else 
        r = ((-g*(-1+e2+e3-(e1*e3)))/((2-e3-e2)*(1-e1)*e3 -(1-e3)*(-1+e2+e3-e1*e3)));



    float d_r=r*(1-e3);
    return ((g - r*(1-e3)))/(2-(e2+e3));
}

float peyman_helper_th(double g, float e1, float e2, float e3)
{
    float r;

    if ((1-e2) < (1-e1)*(e3))
        r = ((1)/((1-e1)*(e3)));
    else 
        r = ((-g*(-1+e2+e3-(e1*e3)))/((2-e3-e2)*(1-e1)*e3 -(1-e3)*(-1+e2+e3-e1*e3)));

    return r*(1-e1);
}

bool r_test(uint8_t e1, uint8_t e2, uint8_t e3)
{
    return ONE - e2 < e3 - e1*e3/ONE;
}

size_t r_val(size_t g, uint8_t e1, uint8_t e2, uint8_t e3)
{
    size_t nom, denom;

    if (r_test(e1, e2, e3)) {
        denom = e3 - e1*e3/ONE;
        return ONE/denom + (ONE % denom != 0);
    } else {
        nom = ONE*g - g*e2 - g*e3 + g*e1*e3/ONE;
        denom = ONE + e1*e3*e2/ONE/ONE - e2 - e1*e3/ONE;
        return nom/denom + (nom % denom != 0);
    }
}

size_t source_budget(size_t g, uint8_t e1, uint8_t e2, uint8_t e3)
{
    size_t nom, denom, r = r_val(g, e1, e2, e3);

    nom = g*ONE + r*ONE - r*e2;
    denom = 2*ONE - e3 - e2;

    return 1.06*nom/denom + (nom % denom != 0);
}

size_t helper_max_budget(size_t g, uint8_t e1, uint8_t e2, uint8_t e3)
{
    size_t nom, denom, r = r_val(g, e1, e2, e3);

    nom = e3*r - r*ONE + g*ONE;
    denom = 2*ONE - e3 - e2;

    return nom/denom + (nom % denom != 0);
}

size_t helper_threshold(size_t g, uint8_t e1, uint8_t e2, uint8_t e3)
{
    size_t r = r_val(g, e1, e2, e3);

    return r - r*e1/ONE;
}

double helper_credit(size_t g, uint8_t e1, uint8_t e2, uint8_t e3)
{
        return (float)ONE/(ONE - e1);
}

size_t relay_credit(size_t g, uint8_t e1, uint8_t e2, uint8_t e3)
{
    return ONE/(ONE - e3*e1/ONE) + (ONE % (ONE - e3*e1/ONE) != 0);
}

void print_usage(const char *arg0)
{
    std::cout << "Usage:" << std::endl;;
    std::cout << "  " << arg0 << " <g> <e1> <e2> <e3>" << std::endl;
    std::cout << std::endl;
    std::cout << "   g: Generation size" << std::endl;
    std::cout << "  e1: Error probability percentage from source to helper" << std::endl;
    std::cout << "  e2: Error probability percentage from helper to relay" << std::endl;
    std::cout << "  e3: Error probability percentage from source to relay" << std::endl;
    std::cout << std::endl;
    std::cout << "Example:" << std::endl;
    std::cout << "  " << arg0 << " 32 10 20 30" << std::endl;
}

size_t read_arg_error(const char *arg)
{
    size_t e = strtol(arg, NULL, 0);
    if (e > 0 && e < 100)
        return (e/100.0)*ONE;

    std::cerr << "Invalid link error value (expected 0 < e < 100, but " << arg << " was given)" << std::endl;
    return -1;
}

int main(int argc, char **argv)
{
    float g, e1, e2, e3;

    if (argc != 5) {
        std::cerr << "Invalid number arguments (expected 4, but " << argc << " was given)" << std::endl;
        print_usage(argv[0]);
        return 1;
    }

    g = strtol(argv[1], NULL, 0);
    if (g == 0) {
        std::cerr << "Invalid generation size (expected g > 0, but " << argv[1] << " was given)" << std::endl;
        return -1;
    }

    if ((e1 = read_arg_error(argv[2])) < 0)
        return 1;

    if ((e2 = read_arg_error(argv[3])) < 0)
        return 1;

    if ((e3 = read_arg_error(argv[4])) < 0)
        return 1;

    std::cout << " g: " << g << std::endl;
    std::cout << "e1: " << argv[2] << "/100 (" << e1 << "/255)" << std::endl;
    std::cout << "e2: " << argv[3] << "/100 (" << e2 << "/255)" << std::endl;
    std::cout << "e3: " << argv[4] << "/100 (" << e3 << "/255)" << std::endl;

    std::cout << "Scaled values:" << std::endl;
    std::cout << "  r" << (r_test(e1, e2, e3) ? "a: " : "b: ") << r_val(g, e1, e2, e3) << std::endl;
    std::cout << "  Bs: " << source_budget(g, e1, e2, e3) << std::endl;
    std::cout << "  Bh: " << helper_max_budget(g, e1, e2, e3) << std::endl;
    std::cout << "  Th: " << helper_threshold(g, e1, e2, e3) << std::endl;
    std::cout << "  Ch: " << helper_credit(g, e1, e2, e3) << std::endl;
    std::cout << "  Cr: " << relay_credit(g, e1, e2, e3) << std::endl;
    std::cout << std::endl;

    e1 = strtol(argv[2], NULL, 0)/100.0;
    e2 = strtol(argv[3], NULL, 0)/100.0;
    e3 = strtol(argv[4], NULL, 0)/100.0;

    std::cout << "Peymans values:" << std::endl;
    std::cout << "   r: " << peyman_r(g, e1, e2, e3) << std::endl;
    std::cout << "  Bs: " << peyman_source(g, e1, e2, e3) << std::endl;
    std::cout << "  Bh: " << peyman_helper(g, e1, e2, e3) << std::endl;
    std::cout << "  Th: " << peyman_helper_th(g, e1, e2, e3) << std::endl;

    return 0;
}
