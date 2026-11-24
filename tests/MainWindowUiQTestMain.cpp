#include "QTestSuiteDispatcher.h"

#include <QApplication>
#include <QtTest/QtTest>

int runStructureScrapDistortionTest(int argc, char **argv);

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    return runSelectedQTestSuites(argc, argv, {
        {"StructureScrapDistortionTest", runStructureScrapDistortionTest}});
}
