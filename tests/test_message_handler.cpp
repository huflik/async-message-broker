// tests/test_message_handler.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "message_handler.hpp"
#include "router.hpp"
#include "mock/mock_storage.hpp"
#include "mock/mock_message_sender.hpp"
#include "mock/mock_config_provider.hpp"

using namespace broker;
using ::testing::_;
using ::testing::Return;
using ::testing::ReturnRef;

class MessageHandlerTest : public ::testing::Test {
protected:
    void SetUp() override {
        config.SessionTimeout = 60;
        config.AckTimeout = 30;
        mock_config.SetConfig(config);

        router = std::make_unique<Router>(mock_storage, mock_sender, mock_config);
    }
    
    MockStorage mock_storage;
    MockMessageSender mock_sender;
    MockConfigProvider mock_config;
    Config config;
    std::unique_ptr<Router> router;
    
    HandlerContext CreateContext(const zmq::message_t& identity) {
        return HandlerContext{
            .storage = mock_storage,
            .session_manager = *router,
            .message_sender = mock_sender,
            .config_provider = mock_config,
            .identity = identity
        };
    }
    
    zmq::message_t CreateIdentity(const std::string& name) {
        zmq::message_t identity(name.size());
        memcpy(identity.data(), name.data(), name.size());
        return identity;
    }
};

TEST_F(MessageHandlerTest, RegisterHandler) {
    auto identity = CreateIdentity("bob");
    auto ctx = CreateContext(identity);
    RegisterHandler handler;
    
    Message msg(MessageType::Register, FlagNone, 0, "bob", "", {});
    
    EXPECT_CALL(mock_storage, LoadPendingMessagesOnly("bob"))
        .WillRepeatedly(Return(std::vector<PendingMessage>()));
    EXPECT_CALL(mock_storage, LoadPendingRepliesForSenderOnly("bob"))
        .WillRepeatedly(Return(std::vector<PendingMessage>()));
    
    EXPECT_NO_THROW(handler.Handle(msg, ctx));
}

TEST_F(MessageHandlerTest, RegisterHandlerEmptyName) {
    auto identity = CreateIdentity("");
    auto ctx = CreateContext(identity);
    RegisterHandler handler;
    
    Message msg(MessageType::Register, FlagNone, 0, "", "", {});
    
    EXPECT_NO_THROW(handler.Handle(msg, ctx));
}

TEST_F(MessageHandlerTest, MessageHandler) {
    auto identity = CreateIdentity("bob");
    auto ctx = CreateContext(identity);
    MessageHandler handler;
    
    Message msg(MessageType::Message, FlagNeedsAck, 12345, "alice", "bob", {0x01});
    
    EXPECT_CALL(mock_storage, SaveMessage(_))
        .WillOnce(Return(1));
    // FindSession - это метод ISessionManager, а не IStorage
    // Не нужно EXPECT_CALL на mock_storage для FindSession
    
    EXPECT_NO_THROW(handler.Handle(msg, ctx));
}

TEST_F(MessageHandlerTest, MessageHandlerWithReply) {
    auto identity = CreateIdentity("bob");
    auto ctx = CreateContext(identity);
    MessageHandler handler;
    
    Message msg(MessageType::Message, FlagNeedsReply | FlagNeedsAck, 12345, "alice", "bob", {0x01});
    
    EXPECT_CALL(mock_storage, SaveMessage(_))
        .WillOnce(Return(1));
    EXPECT_CALL(mock_storage, SaveCorrelation(1, 12345, "alice"))
        .Times(1);
    
    EXPECT_NO_THROW(handler.Handle(msg, ctx));
}

TEST_F(MessageHandlerTest, ReplyHandler) {
    auto identity = CreateIdentity("bob");
    auto ctx = CreateContext(identity);
    ReplyHandler handler;
    
    Message msg(MessageType::Reply, FlagNone, 12345, "bob", "alice", {0x02});
    
    EXPECT_CALL(mock_storage, FindOriginalSenderByCorrelation(12345))
        .WillOnce(Return("alice"));
    EXPECT_CALL(mock_storage, SaveMessage(_))
        .WillOnce(Return(2));
    EXPECT_CALL(mock_storage, SaveCorrelation(2, 12345, "alice"))
        .Times(1);
    
    EXPECT_NO_THROW(handler.Handle(msg, ctx));
}

TEST_F(MessageHandlerTest, AckHandler) {
    auto identity = CreateIdentity("bob");
    auto ctx = CreateContext(identity);
    AckHandler handler;
    
    Message msg(MessageType::Ack, FlagNone, 12345, "bob", "", {});
    
    EXPECT_CALL(mock_storage, FindMessageIdByCorrelationAndDestination(12345, "bob"))
        .WillOnce(Return(1));
    EXPECT_CALL(mock_storage, NeedsAck(1))
        .WillOnce(Return(true));
    EXPECT_CALL(mock_storage, MarkDelivered(1))
        .Times(1);
    EXPECT_CALL(mock_storage, MarkAckReceived(1, "bob"))
        .Times(1);
    
    EXPECT_NO_THROW(handler.Handle(msg, ctx));
}

TEST_F(MessageHandlerTest, AckHandlerZeroCorrelation) {
    auto identity = CreateIdentity("bob");
    auto ctx = CreateContext(identity);
    AckHandler handler;
    
    Message msg(MessageType::Ack, FlagNone, 0, "bob", "", {});
    
    EXPECT_NO_THROW(handler.Handle(msg, ctx));
}

TEST_F(MessageHandlerTest, UnregisterHandler) {
    auto identity = CreateIdentity("bob");
    auto ctx = CreateContext(identity);
    UnregisterHandler handler;
    
    Message msg(MessageType::Unregister, FlagNone, 0, "bob", "", {});
    
    EXPECT_NO_THROW(handler.Handle(msg, ctx));
}

TEST_F(MessageHandlerTest, MessageHandlerFactory) {
    EXPECT_NE(MessageHandlerFactory::Create(MessageType::Register), nullptr);
    EXPECT_NE(MessageHandlerFactory::Create(MessageType::Message), nullptr);
    EXPECT_NE(MessageHandlerFactory::Create(MessageType::Reply), nullptr);
    EXPECT_NE(MessageHandlerFactory::Create(MessageType::Ack), nullptr);
    EXPECT_NE(MessageHandlerFactory::Create(MessageType::Unregister), nullptr);
    EXPECT_EQ(MessageHandlerFactory::Create(static_cast<MessageType>(99)), nullptr);
}