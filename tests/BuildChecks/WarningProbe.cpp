// Deliberately triggers an "unused variable" warning. It is never part of the
// normal build; only the Build.Warnings.AreErrors test builds it, and expects
// the build to FAIL because warnings are errors.
int main()
{
    int unusedValue = 42;
    return 0;
}
