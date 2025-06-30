#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "magda/core/command.hpp"
#include <nlohmann/json.hpp>
#include <vector>

TEST_CASE("Command Creation and Basic Operations", "[command]") {
    SECTION("Create command with type") {
        Command cmd("play");
        REQUIRE(cmd.getType() == "play");
    }
    
    SECTION("Set and get string parameter") {
        Command cmd("createTrack");
        cmd.setParameter("name", std::string("Bass Track"));
        REQUIRE(cmd.getParameter<std::string>("name") == "Bass Track");
    }
    
    SECTION("Set and get numeric parameters") {
        Command cmd("setVolume");
        cmd.setParameter("volume", 0.75);
        cmd.setParameter("trackId", 42);
        
        REQUIRE(cmd.getParameter<double>("volume") == Catch::Approx(0.75));
        REQUIRE(cmd.getParameter<int>("trackId") == 42);
    }
    
    SECTION("Set and get boolean parameter") {
        Command cmd("setMute");
        cmd.setParameter("muted", true);
        REQUIRE(cmd.getParameter<bool>("muted") == true);
    }
    
    SECTION("Set and get vector parameter") {
        Command cmd("addMidiClip");
        std::vector<double> notes = {60.0, 64.0, 67.0};
        cmd.setParameter("notes", notes);
        
        auto retrieved = cmd.getParameter<std::vector<double>>("notes");
        REQUIRE(retrieved.size() == 3);
        REQUIRE(retrieved[0] == Catch::Approx(60.0));
        REQUIRE(retrieved[1] == Catch::Approx(64.0));
        REQUIRE(retrieved[2] == Catch::Approx(67.0));
    }
    
    SECTION("Check parameter existence") {
        Command cmd("test");
        cmd.setParameter("exists", 123);
        
        REQUIRE(cmd.hasParameter("exists") == true);
        REQUIRE(cmd.hasParameter("doesNotExist") == false);
    }
}

TEST_CASE("Command JSON Serialization", "[command]") {
    SECTION("Convert command to JSON") {
        Command cmd("addMidiClip");
        cmd.setParameter("trackId", std::string("track_1"));
        cmd.setParameter("start", 4.0);
        cmd.setParameter("length", 2.0);
        
        nlohmann::json json = cmd.toJson();
        
        REQUIRE(json["command"] == "addMidiClip");
        REQUIRE(json["trackId"] == "track_1");
        REQUIRE(json["start"] == Catch::Approx(4.0));
        REQUIRE(json["length"] == Catch::Approx(2.0));
    }
    
    SECTION("Create command from JSON") {
        nlohmann::json json = {
            {"command", "play"},
            {"position", 10.5},
            {"loop", true}
        };
        
        Command cmd(json);
        
        REQUIRE(cmd.getType() == "play");
        REQUIRE(cmd.getParameter<double>("position") == Catch::Approx(10.5));
        REQUIRE(cmd.getParameter<bool>("loop") == true);
    }
    
    SECTION("JSON string conversion") {
        Command cmd("stop");
        cmd.setParameter("fadeOut", 1.0);
        
        std::string json_str = cmd.toJsonString();
        Command cmd2 = Command::fromJsonString(json_str);
        
        REQUIRE(cmd2.getType() == "stop");
        REQUIRE(cmd2.getParameter<double>("fadeOut") == Catch::Approx(1.0));
    }
}

TEST_CASE("CommandResponse", "[command]") {
    SECTION("Create success response") {
        CommandResponse response(CommandResponse::Status::Success, "Operation completed");
        
        REQUIRE(response.getStatus() == CommandResponse::Status::Success);
        REQUIRE(response.getMessage() == "Operation completed");
    }
    
    SECTION("Create error response") {
        CommandResponse response(CommandResponse::Status::Error, "Track not found");
        
        REQUIRE(response.getStatus() == CommandResponse::Status::Error);
        REQUIRE(response.getMessage() == "Track not found");
    }
    
    SECTION("Response with data") {
        CommandResponse response(CommandResponse::Status::Success);
        nlohmann::json data = {{"trackId", "track_123"}, {"name", "New Track"}};
        response.setData(data);
        
        REQUIRE(response.getData()["trackId"] == "track_123");
        REQUIRE(response.getData()["name"] == "New Track");
    }
    
    SECTION("Convert response to JSON") {
        CommandResponse response(CommandResponse::Status::Pending, "Processing...");
        nlohmann::json data = {{"progress", 0.5}};
        response.setData(data);
        
        nlohmann::json json = response.toJson();
        
        REQUIRE(json["status"] == "pending");
        REQUIRE(json["message"] == "Processing...");
        REQUIRE(json["data"]["progress"] == Catch::Approx(0.5));
    }
} 