// tests/test_session.cpp
#include <gtest/gtest.h>
#include <chrono>
#include <thread>
#include "session.hpp"

using namespace broker;

// Простая заглушка для IMessageSender
class TestMessageSender : public IMessageSender {
public:
    void SendToClient(zmq::message_t identity, 
                      zmq::message_t data,
                      std::function<void(bool)> callback) override {
        last_identity = std::move(identity);
        last_data = std::move(data);
        send_called = true;
        if (callback) {
            callback(true);
        }
    }
    
    bool send_called = false;
    zmq::message_t last_identity;
    zmq::message_t last_data;
};

class SessionTest : public ::testing::Test {
protected:
    void SetUp() override {
        config.SessionTimeout = 60;
        config.AckTimeout = 30;
        
        zmq::message_t identity(3);
        memcpy(identity.data(), "bob", 3);
        
        sender = std::make_unique<TestMessageSender>();
        session = std::make_unique<Session>(std::move(identity), *sender, config);
        session->SetName("bob");
    }
    
    std::unique_ptr<TestMessageSender> sender;
    Config config;
    std::unique_ptr<Session> session;
};

TEST_F(SessionTest, GetName) {
    EXPECT_EQ(session->GetName(), "bob");
}

TEST_F(SessionTest, IsOnlineInitially) {
    EXPECT_TRUE(session->IsOnline());
}

TEST_F(SessionTest, MarkOffline) {
    session->MarkOffline();
    EXPECT_FALSE(session->IsOnline());
}

TEST_F(SessionTest, MarkOnline) {
    session->MarkOffline();
    session->MarkOnline();
    EXPECT_TRUE(session->IsOnline());
}

TEST_F(SessionTest, UpdateLastReceive) {
    EXPECT_NO_THROW(session->UpdateLastReceive());
}

TEST_F(SessionTest, UpdateLastActivity) {
    EXPECT_NO_THROW(session->UpdateLastActivity());
}

TEST_F(SessionTest, IsExpiredWhenOffline) {
    session->MarkOffline();
    EXPECT_TRUE(session->IsExpired(60));
}

TEST_F(SessionTest, IsNotExpiredWhenRecentlyActive) {
    session->UpdateLastReceive();
    EXPECT_FALSE(session->IsExpired(60));
}

TEST_F(SessionTest, IsExpiredAfterTimeout) {
    session->UpdateLastReceive();
    std::this_thread::sleep_for(std::chrono::milliseconds(2000));
    EXPECT_TRUE(session->IsExpired(1));
}

TEST_F(SessionTest, SendMessageWhenOnline) {
    Message msg(MessageType::Message, FlagNone, 0, "alice", "bob", {0x01, 0x02});
    
    bool result = session->SendMessage(msg);
    EXPECT_TRUE(result);
    EXPECT_TRUE(sender->send_called);
}

TEST_F(SessionTest, SendMessageWhenOfflineQueues) {
    session->MarkOffline();
    Message msg(MessageType::Message, FlagNone, 0, "alice", "bob", {0x01});
    
    sender->send_called = false;
    bool result = session->SendMessage(msg);
    
    EXPECT_TRUE(result);
    EXPECT_FALSE(sender->send_called);
    EXPECT_EQ(session->GetQueueSize(), 1);
}

TEST_F(SessionTest, FlushQueueAfterComingOnline) {
    session->MarkOffline();
    
    Message msg1(MessageType::Message, FlagNone, 0, "alice", "bob", {0x01});
    Message msg2(MessageType::Message, FlagNone, 0, "alice", "bob", {0x02});
    
    session->SendMessage(msg1);
    session->SendMessage(msg2);
    EXPECT_EQ(session->GetQueueSize(), 2);
    
    sender->send_called = false;
    session->MarkOnline();
    session->FlushQueue();
    
    EXPECT_EQ(session->GetQueueSize(), 0);
    EXPECT_TRUE(sender->send_called);
}

TEST_F(SessionTest, SendMessageWithAckFlag) {
    Message msg(MessageType::Message, FlagNeedsAck, 12345, "alice", "bob", {0x01});
    
    bool result = session->SendMessage(msg);
    EXPECT_TRUE(result);
    EXPECT_TRUE(sender->send_called);
}

TEST_F(SessionTest, SendMessageWithReplyFlag) {
    Message msg(MessageType::Message, FlagNeedsReply, 12345, "alice", "bob", {0x01});
    
    bool result = session->SendMessage(msg);
    EXPECT_TRUE(result);
    EXPECT_TRUE(sender->send_called);
}

TEST_F(SessionTest, GetQueueSize) {
    EXPECT_EQ(session->GetQueueSize(), 0);
    
    session->MarkOffline();
    session->SendMessage(Message(MessageType::Message, FlagNone, 0, "a", "b", {0x01}));
    EXPECT_EQ(session->GetQueueSize(), 1);
    
    session->SendMessage(Message(MessageType::Message, FlagNone, 0, "a", "b", {0x02}));
    EXPECT_EQ(session->GetQueueSize(), 2);
}