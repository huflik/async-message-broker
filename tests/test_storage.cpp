#include <gtest/gtest.h>
#include <filesystem>
#include "storage.hpp"

using namespace broker;


class StorageTest : public ::testing::Test {
protected:
    void SetUp() override {
        test_db_path = "./test_storage.db";
        storage = std::make_unique<Storage>(test_db_path);
    }
    
    void TearDown() override {
        storage.reset();
        std::filesystem::remove(test_db_path);
    }
    
    std::unique_ptr<Storage> storage;
    std::string test_db_path;
    
    Message CreateTestMessage(MessageType type = MessageType::Message,
                              uint64_t corr_id = 12345,
                              const std::string& sender = "alice",
                              const std::string& dest = "bob") {
        return Message(type, FlagNeedsAck, corr_id, sender, dest, {0x01, 0x02, 0x03});
    }
};

TEST_F(StorageTest, SaveAndLoadMessage) {
    auto msg = CreateTestMessage();
    uint64_t id = storage->SaveMessage(msg);
    EXPECT_GT(id, 0);
    
    auto pending = storage->LoadPendingMessagesOnly("bob");
    EXPECT_EQ(pending.size(), 1);
    EXPECT_EQ(pending[0].id, id);
    EXPECT_EQ(pending[0].msg.GetSender(), "alice");
}

TEST_F(StorageTest, MarkDelivered) {
    uint64_t id = storage->SaveMessage(CreateTestMessage());
    storage->MarkDelivered(id);
    
    auto pending = storage->LoadPendingMessagesOnly("bob");
    EXPECT_TRUE(pending.empty());
}

TEST_F(StorageTest, MarkSent) {
    uint64_t id = storage->SaveMessage(CreateTestMessage());
    storage->MarkSent(id);
    
    EXPECT_TRUE(storage->NeedsAck(id));
}

TEST_F(StorageTest, NeedsAck) {
    Message msg(MessageType::Message, FlagNeedsAck, 12345, "alice", "bob", {});
    
    uint64_t id1 = storage->SaveMessage(msg);
    
    storage->MarkSent(id1);
    
    EXPECT_TRUE(storage->NeedsAck(id1));
    
    Message msg2(MessageType::Message, FlagNone, 12346, "alice", "bob", {});
    uint64_t id2 = storage->SaveMessage(msg2);
    storage->MarkSent(id2);
    
    EXPECT_FALSE(storage->NeedsAck(id2));
}

TEST_F(StorageTest, SaveAndFindCorrelation) {
    uint64_t msg_id = storage->SaveMessage(CreateTestMessage());
    storage->SaveCorrelation(msg_id, 12345, "alice");
    
    std::string sender = storage->FindOriginalSenderByCorrelation(12345);
    EXPECT_EQ(sender, "alice");
    
    uint64_t found_id = storage->FindMessageIdByCorrelation(12345);
    EXPECT_EQ(found_id, msg_id);
}

TEST_F(StorageTest, MarkAckReceivedFromOriginalSender) {
    uint64_t msg_id = storage->SaveMessage(CreateTestMessage());
    storage->SaveCorrelation(msg_id, 12345, "alice");
    
    storage->MarkAckReceived(msg_id, "alice");
    
    std::string sender = storage->FindOriginalSenderByCorrelation(12345);
    EXPECT_TRUE(sender.empty());
}

TEST_F(StorageTest, MarkAckReceivedFromWrongSender) {
    uint64_t msg_id = storage->SaveMessage(CreateTestMessage());
    storage->SaveCorrelation(msg_id, 12345, "alice");
    
    storage->MarkAckReceived(msg_id, "bob");
    
    std::string sender = storage->FindOriginalSenderByCorrelation(12345);
    EXPECT_EQ(sender, "alice");
}

TEST_F(StorageTest, LoadPendingMessagesOnly) {
    storage->SaveMessage(CreateTestMessage(MessageType::Message, 111, "alice", "bob"));
    
    storage->SaveMessage(CreateTestMessage(MessageType::Reply, 222, "bob", "alice"));
    
    auto pending = storage->LoadPendingMessagesOnly("bob");
    EXPECT_EQ(pending.size(), 1);
    EXPECT_EQ(pending[0].msg.GetType(), MessageType::Message);
}

TEST_F(StorageTest, LoadPendingRepliesForSenderOnly) {
    uint64_t msg_id = storage->SaveMessage(CreateTestMessage(MessageType::Message, 12345, "alice", "bob"));
    storage->SaveCorrelation(msg_id, 12345, "alice");
    
    Message reply(MessageType::Reply, FlagNone, 12345, "bob", "alice", {});
    uint64_t reply_id = storage->SaveMessage(reply);
    storage->SaveCorrelation(reply_id, 12345, "alice");
    
    auto replies = storage->LoadPendingRepliesForSenderOnly("alice");
    EXPECT_EQ(replies.size(), 1);
    EXPECT_EQ(replies[0].msg.GetType(), MessageType::Reply);
}

TEST_F(StorageTest, FindMessageIdByCorrelationAndDestination) {
    uint64_t msg_id = storage->SaveMessage(CreateTestMessage(MessageType::Message, 12345, "alice", "bob"));
    storage->SaveCorrelation(msg_id, 12345, "alice");
    
    uint64_t found_id = storage->FindMessageIdByCorrelationAndDestination(12345, "bob");
    EXPECT_EQ(found_id, msg_id);
    
    uint64_t not_found = storage->FindMessageIdByCorrelationAndDestination(12345, "charlie");
    EXPECT_EQ(not_found, 0);
}

TEST_F(StorageTest, MarkPending) {
    uint64_t id = storage->SaveMessage(CreateTestMessage());
    
    auto pending_initial = storage->LoadPendingMessagesOnly("bob");
    bool found_initial = false;
    for (const auto& msg : pending_initial) {
        if (msg.id == id) {
            found_initial = true;
            break;
        }
    }
    EXPECT_TRUE(found_initial) << "Message should be in PENDING status initially";
    
    storage->MarkSent(id);
    
    auto pending_after_sent = storage->LoadPendingMessagesOnly("bob");
    bool found_after_sent = false;
    for (const auto& msg : pending_after_sent) {
        if (msg.id == id) {
            found_after_sent = true;
            break;
        }
    }
    EXPECT_FALSE(found_after_sent) << "Message should not be in PENDING after MarkSent";
    
    storage->MarkPending(id);
    
    auto pending_after_pending = storage->LoadPendingMessagesOnly("bob");
    bool found_after_pending = false;
    for (const auto& msg : pending_after_pending) {
        if (msg.id == id) {
            found_after_pending = true;
            break;
        }
    }
    EXPECT_TRUE(found_after_pending) << "Message should be in PENDING after MarkPending";
    
    storage->MarkSent(id);
    
    auto pending_final = storage->LoadPendingMessagesOnly("bob");
    bool found_final = false;
    for (const auto& msg : pending_final) {
        if (msg.id == id) {
            found_final = true;
            break;
        }
    }
    EXPECT_FALSE(found_final) << "Message should not be in PENDING after second MarkSent";
}