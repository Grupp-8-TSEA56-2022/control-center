#include "catch.hpp"
#include "map_node.h"
#include "control_center.h"

using namespace std;
TEST_CASE("Map Node") {
    SECTION("Constructors") {
        MapNode node1{"1", nullptr, nullptr};
        MapNode node2{"2", &node1, nullptr, 34};
        MapNode node3{"3", &node1, &node2};
    }
    SECTION("Constructors (pointers)") {
        MapNode *node1 = new MapNode{"1", nullptr, nullptr};
        MapNode *node2 = new MapNode{"2", node1, nullptr, 34};
        MapNode *node3 = new MapNode{"3", node1, node2};
        delete node1;
        delete node2;
        delete node3;
    }
    SECTION("Getters and setters") {
        MapNode node1{"1", nullptr, nullptr};
        MapNode node2{"2", &node1, nullptr, 34};
        MapNode node3{"3", &node1, &node2};
        CHECK(node1.get_weight() > 100000);
        CHECK(node2.get_weight() == 34);
        node1.set_weight(8);
        CHECK(node1.get_weight() == 8);
    }
    SECTION("Get next") {
        MapNode node1{"1", nullptr, nullptr};
        MapNode node2{"2", &node1, nullptr, 34};
        MapNode node3{"3", &node1, &node2};

        CHECK(*node2.get_next_left() == node1);
        //CHECK(node2.get_next_right() != node1);
    }
}

TEST_CASE("Control Center") {
    SECTION("Basics") {
        ControlCenter control_center{};
        drive_intstruction_t instr{instructions::left, 1};
        control_center.add_drive_instruction(instr);
    }

}
