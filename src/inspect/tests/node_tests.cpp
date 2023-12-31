#include <gtest/gtest.h>

#include "src/inspect/kb/node.hpp"

using namespace sx;

struct KB_String : public Node_Data {
    KB_String() = default;
    KB_String(std::string const& s) : value(s) {};
    std::string value;
    nlohmann::json to_json() const override { return value; };
    std::string to_string() const override { return "value=" + value + "\""; };
};

struct KB_Int : public Node_Data {
    KB_Int() = default;
    KB_Int(int i) : value(i) {};
    int value;
    nlohmann::json to_json() const override { return value; };
    std::string to_string() const override { return "value=" + std::to_string(value) + "\""; };
};

struct KB_XY : public Node_Data {
    KB_XY() = default;
    KB_XY(double x, double y) : x(x), y(y) {};
    double x;
    double y;

    nlohmann::json to_json() const override { return { x, y }; };
    std::string to_string() const override {
        std::stringstream ss;
        ss << "value=(" << x << "," << y << ")";
        return ss.str();
    };
};


TEST(KB_test, test_string) {

    auto root_kb = std::make_shared<KB_String>();
    //root_kb->value = "root";
    Node<std::string> root;


    auto new_kb = std::make_shared<KB_String>();
    new_kb->value = "some";
    auto one = root.replace("1", new_kb);

    auto new_kb2 = std::make_shared<KB_String>();
    new_kb2->value = "some2";
    auto two = one->replace("2", new_kb2);

    two->at<KB_String>("3", "some")->at<KB_String>("another", "some_value");


    auto sub_kb = std::make_shared<KB_String>();
    sub_kb->value = "sub";
    one->replace("10", sub_kb);


    auto x = root.to_json();
    std::cout << x.dump(4) << "\n";

    std::cout << one->to_json().dump(4) << "\n";
    std::cout << "\n";
    std::cout << "\n";
    std::cout << "\n";

}

TEST(KB_test, test_mixed) {

    auto root_kb = std::make_shared<KB_Int>();
    Node<std::string> root;
    root.at<KB_String>("header1", "abc");
    root.at<KB_String>("header2", "123");
    root.at<KB_Int>("Content-Length", 12356);
    auto coo = root.at<KB_XY>("Coords", -3453, 345);
    coo->label = "xy";
    root["Coords"].lock()->label = "XY";
    coo->at<KB_Int>("Z", 0);

    auto x = root.to_json();
    std::cout << x.dump(4) << "\n";

//    std::cout << to_string(one->to_json()) << "\n";
    std::cout << "\n";
    std::cout << "\n";
    std::cout << "\n";

}

TEST(KB_test, test_quota) {

    auto root_kb = std::make_shared<KB_Int>();
    Node<std::string> root;
    root.max_elements = 10;

    for (int i = 0; i < 1000; ++i) {
        root.at<KB_String>("header"+std::to_string(i), "abc" + std::to_string(i));
    }

    auto x = root.to_json();
    std::cout << x.dump(4) << "\n";

    std::cout << "\n";
    std::cout << "\n";
    std::cout << "\n";

}