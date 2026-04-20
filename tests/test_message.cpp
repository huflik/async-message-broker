#include <gtest/gtest.h>
#include "message.hpp"

using namespace broker;

class MessageTest : public ::testing::Test {
protected:
    void SetUp() override {
        payload = {0x01, 0x02, 0x03, 0x04};
    }
    
    std::vector<uint8_t> payload;
};

TEST_F(MessageTest, DefaultConstructor) {
    Message msg;
    EXPECT_EQ(msg.GetType(), MessageType::Message);
    EXPECT_EQ(msg.GetFlags(), FlagNone);
    EXPECT_EQ(msg.GetCorrelationId(), 0);
    EXPECT_TRUE(msg.GetSender().empty());
    EXPECT_TRUE(msg.GetDestination().empty());
    EXPECT_TRUE(msg.GetPayload().empty());
}

TEST_F(MessageTest, ParameterizedConstructor) {
    Message msg(MessageType::Register, FlagNeedsAck, 12345, "alice", "bob", payload);
    
    EXPECT_EQ(msg.GetType(), MessageType::Register);
    EXPECT_EQ(msg.GetFlags(), FlagNeedsAck);
    EXPECT_EQ(msg.GetCorrelationId(), 12345);
    EXPECT_EQ(msg.GetSender(), "alice");
    EXPECT_EQ(msg.GetDestination(), "bob");
    EXPECT_EQ(msg.GetPayload(), payload);
}

TEST_F(MessageTest, FlagOperations) {
    Message msg;
    
    msg.SetNeedsReply(true);
    EXPECT_TRUE(msg.NeedsReply());
    EXPECT_TRUE(msg.HasFlag(FlagNeedsReply));
    
    msg.SetNeedsAck(true);
    EXPECT_TRUE(msg.NeedsAck());
    EXPECT_TRUE(msg.HasFlag(FlagNeedsAck));
    
    msg.SetNeedsReply(false);
    EXPECT_FALSE(msg.NeedsReply());
    
    msg.SetNeedsAck(false);
    EXPECT_FALSE(msg.NeedsAck());
}

TEST_F(MessageTest, SerializationAndDeserialization) {
    Message original(MessageType::Message, FlagNeedsAck | FlagNeedsReply, 
                     54321, "sender", "receiver", payload);
    
    auto serialized = original.Serialize();
    auto deserialized = Message::Deserialize(serialized);
    
    EXPECT_EQ(deserialized.GetType(), original.GetType());
    EXPECT_EQ(deserialized.GetFlags(), original.GetFlags());
    EXPECT_EQ(deserialized.GetCorrelationId(), original.GetCorrelationId());
    EXPECT_EQ(deserialized.GetSender(), original.GetSender());
    EXPECT_EQ(deserialized.GetDestination(), original.GetDestination());
    EXPECT_EQ(deserialized.GetPayload(), original.GetPayload());
}

TEST_F(MessageTest, SerializationEmptyPayload) {
    Message original(MessageType::Ack, FlagNone, 0, "alice", "", {});
    
    auto serialized = original.Serialize();
    auto deserialized = Message::Deserialize(serialized);
    
    EXPECT_TRUE(deserialized.GetPayload().empty());
}

TEST_F(MessageTest, DeserializeTooShort) {
    std::vector<uint8_t> short_data = {0x01, 0x02};
    EXPECT_THROW(Message::Deserialize(short_data), std::runtime_error);
}

TEST_F(MessageTest, DeserializeWrongVersion) {
    std::vector<uint8_t> data = {0xFF, 0x02, 0x00, 0x00, 0x00, 0x00, 0x00, 
                                  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
    EXPECT_THROW(Message::Deserialize(data), std::runtime_error);
}

TEST_F(MessageTest, ToString) {
    Message msg(MessageType::Register, FlagNeedsAck, 123, "alice", "bob", {});
    std::string str = msg.ToString();
    
    EXPECT_TRUE(str.find("Register") != std::string::npos);
    EXPECT_TRUE(str.find("alice") != std::string::npos);
    EXPECT_TRUE(str.find("bob") != std::string::npos);
}