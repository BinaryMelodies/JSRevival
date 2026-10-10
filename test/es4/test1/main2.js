
import TransientPackage.CompletePackage;
import TransientPackage.PartialPackage.PartialFunction;
import TransientPackage.PartialPackage.OtherPartialFunction;

print("Start");

CompleteFunction();
TransientPackage.CompletePackage.CompleteFunction();
PartialFunction();
TransientPackage.PartialPackage.PartialFunction();
OtherPartialFunction();
TransientPackage.PartialPackage.OtherPartialFunction();

