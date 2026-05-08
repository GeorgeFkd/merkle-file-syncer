#include <gtest/gtest.h>
#include <QCoreApplication>

class QtEnvironment : public ::testing::Environment {
public:
    void SetUp() override {
        int argc = 0;
        app = new QCoreApplication(argc, nullptr);
    }
    void TearDown() override {
        delete app;
    }
private:
    QCoreApplication *app = nullptr;
};

int main(int argc, char *argv[]) {
    ::testing::InitGoogleTest(&argc, argv);
    ::testing::AddGlobalTestEnvironment(new QtEnvironment);
    return RUN_ALL_TESTS();
}
