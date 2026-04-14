// tests/test_router.cpp
#include <gtest/gtest.h>
#include <gmock/gmock.h>
#include "router.hpp"
#include "mock/mock_storage.hpp"
#include "mock/mock_message_sender.hpp"
#include "mock/mock_config_provider.hpp"

using namespace broker;
using ::testing::_;
using ::testing::Return;
using ::testing::ReturnRef;

class RouterTest : public ::testing::Test {
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
    
    zmq::message_t CreateIdentity(const std::string& name) {
        zmq::message_t identity(name.size());
        memcpy(identity.data(), name.data(), name.size());
        return identity;
    }
    
    Message CreateRegisterMessage(const std::string& name) {
        return Message(MessageType::Register, FlagNone, 0, name, "", {});
    }
};

TEST_F(RouterTest, RegisterClient) {
    auto identity = CreateIdentity("bob");
    auto session = std::make_shared<Session>(std::move(identity), mock_sender, config);
    
    // Важно: устанавливаем имя сессии!
    session->SetName("bob");
    
    bool result = router->RegisterClient("bob", session);
    EXPECT_TRUE(result);
    
    auto found = router->FindSession("bob");
    EXPECT_NE(found, nullptr);
    EXPECT_EQ(found->GetName(), "bob");
}

TEST_F(RouterTest, RegisterDuplicateClient) {
    auto identity1 = CreateIdentity("bob");
    auto session1 = std::make_shared<Session>(std::move(identity1), mock_sender, config);
    session1->SetName("bob");
    
    auto identity2 = CreateIdentity("bob2");
    auto session2 = std::make_shared<Session>(std::move(identity2), mock_sender, config);
    session2->SetName("bob");
    
    EXPECT_TRUE(router->RegisterClient("bob", session1));
    EXPECT_FALSE(router->RegisterClient("bob", session2));
}

TEST_F(RouterTest, UnregisterClient) {
    auto identity = CreateIdentity("bob");
    auto session = std::make_shared<Session>(std::move(identity), mock_sender, config);
    session->SetName("bob");
    
    router->RegisterClient("bob", session);
    router->UnregisterClient("bob");
    
    auto found = router->FindSession("bob");
    EXPECT_EQ(found, nullptr);
}

TEST_F(RouterTest, FindSessionNotFound) {
    auto found = router->FindSession("nonexistent");
    EXPECT_EQ(found, nullptr);
}

TEST_F(RouterTest, HandleRegisterMessage) {
    auto identity = CreateIdentity("bob");
    auto msg = CreateRegisterMessage("bob");
    
    EXPECT_CALL(mock_storage, LoadPendingMessagesOnly("bob"))
        .WillOnce(Return(std::vector<PendingMessage>()));
    EXPECT_CALL(mock_storage, LoadPendingRepliesForSenderOnly("bob"))
        .WillOnce(Return(std::vector<PendingMessage>()));
    
    router->RouteMessage(msg, identity);
    
    auto found = router->FindSession("bob");
    EXPECT_NE(found, nullptr);
    EXPECT_EQ(found->GetName(), "bob");
}

TEST_F(RouterTest, HandleMessageOnline) {
    // Сначала регистрируем bob
    auto identity = CreateIdentity("bob");
    auto register_msg = CreateRegisterMessage("bob");
    
    EXPECT_CALL(mock_storage, LoadPendingMessagesOnly("bob"))
        .WillRepeatedly(Return(std::vector<PendingMessage>()));
    EXPECT_CALL(mock_storage, LoadPendingRepliesForSenderOnly("bob"))
        .WillRepeatedly(Return(std::vector<PendingMessage>()));
    
    router->RouteMessage(register_msg, identity);
    
    // Отправляем сообщение bob
    Message msg(MessageType::Message, FlagNone, 12345, "alice", "bob", {0x01});
    
    EXPECT_CALL(mock_storage, SaveMessage(_))
        .WillOnce(Return(1));
    EXPECT_CALL(mock_sender, SendToClient(_, _, _))
        .WillOnce([](zmq::message_t identity, zmq::message_t data, std::function<void(bool)> callback) {
            callback(true);
        });
    
    router->RouteMessage(msg, identity);
}

TEST_F(RouterTest, HandleMessageOffline) {
    Message msg(MessageType::Message, FlagNone, 12345, "alice", "bob", {0x01});
    
    EXPECT_CALL(mock_storage, SaveMessage(_))
        .WillOnce(Return(1));
    EXPECT_CALL(mock_sender, SendToClient(_, _, _)).Times(0);
    
    // bob не зарегистрирован - сообщение сохранится в БД
    router->RouteMessage(msg, CreateIdentity("alice"));
}

TEST_F(RouterTest, HandleReply) {
    // Регистрируем alice
    auto identity_alice = CreateIdentity("alice");
    auto register_alice = CreateRegisterMessage("alice");
    
    EXPECT_CALL(mock_storage, LoadPendingMessagesOnly(_))
        .WillRepeatedly(Return(std::vector<PendingMessage>()));
    EXPECT_CALL(mock_storage, LoadPendingRepliesForSenderOnly(_))
        .WillRepeatedly(Return(std::vector<PendingMessage>()));
    
    router->RouteMessage(register_alice, identity_alice);
    
    // Отправляем Reply
    Message reply(MessageType::Reply, FlagNone, 12345, "bob", "alice", {0x02});
    
    EXPECT_CALL(mock_storage, FindOriginalSenderByCorrelation(12345))
        .WillOnce(Return("alice"));
    EXPECT_CALL(mock_storage, SaveMessage(_))
        .WillOnce(Return(2));
    EXPECT_CALL(mock_storage, SaveCorrelation(_, _, _))
        .Times(1);
    EXPECT_CALL(mock_sender, SendToClient(_, _, _))
        .WillOnce([](zmq::message_t identity, zmq::message_t data, std::function<void(bool)> callback) {
            callback(true);
        });
    
    router->RouteMessage(reply, CreateIdentity("bob"));
}

TEST_F(RouterTest, HandleAck) {
    Message ack(MessageType::Ack, FlagNone, 12345, "bob", "", {});
    
    EXPECT_CALL(mock_storage, FindMessageIdByCorrelationAndDestination(12345, "bob"))
        .WillOnce(Return(1));
    EXPECT_CALL(mock_storage, NeedsAck(1))
        .WillOnce(Return(true));
    EXPECT_CALL(mock_storage, MarkDelivered(1))
        .Times(1);
    EXPECT_CALL(mock_storage, MarkAckReceived(1, "bob"))
        .Times(1);
    
    router->RouteMessage(ack, CreateIdentity("bob"));
}

TEST_F(RouterTest, HandleAckUnknownCorrelation) {
    Message ack(MessageType::Ack, FlagNone, 99999, "bob", "", {});
    
    EXPECT_CALL(mock_storage, FindMessageIdByCorrelationAndDestination(99999, "bob"))
        .WillOnce(Return(0));
    EXPECT_CALL(mock_storage, FindMessageIdByCorrelation(99999))
        .WillOnce(Return(0));
    EXPECT_CALL(mock_storage, MarkDelivered(_)).Times(0);
    
    router->RouteMessage(ack, CreateIdentity("bob"));
}

TEST_F(RouterTest, PrintActiveClients) {
    auto identity = CreateIdentity("bob");
    auto session = std::make_shared<Session>(std::move(identity), mock_sender, config);
    session->SetName("bob");
    
    router->RegisterClient("bob", session);
    
    EXPECT_NO_THROW(router->PrintActiveClients());
}

// Дополнительный тест для проверки корректной работы RegisterClient
TEST_F(RouterTest, RegisterClientSetsCorrectName) {
    auto identity = CreateIdentity("alice");
    auto session = std::make_shared<Session>(std::move(identity), mock_sender, config);
    
    // Устанавливаем имя
    session->SetName("alice");
    
    bool result = router->RegisterClient("alice", session);
    EXPECT_TRUE(result);
    
    auto found = router->FindSession("alice");
    ASSERT_NE(found, nullptr);
    EXPECT_EQ(found->GetName(), "alice");
}