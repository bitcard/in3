using System;
using System.Numerics;
using System.Threading.Tasks;
using In3;

namespace ConnectToEthereum
{
    class Program
    {
        static async Task Main()
        {
            Console.Out.WriteLine("Ethereum Main Network");
            IN3 mainnetClient = IN3.ForChain(Chain.Mainnet);
            BigInteger mainnetLatest = await mainnetClient.Eth1.BlockNumber();
            BigInteger mainnetCurrentGasPrice = await mainnetClient.Eth1.GetGasPrice();
            Console.Out.WriteLine($"Latest Block Number: {mainnetLatest}");
            Console.Out.WriteLine($"Gas Price: {mainnetCurrentGasPrice} Wei");

            Console.Out.WriteLine("Ethereum EWC Network");
            IN3 ewcClient = IN3.ForChain(Chain.Ewc);
            BigInteger ewcLatest = await ewcClient.Eth1.BlockNumber();
            BigInteger ewcCurrentGasPrice = await ewcClient.Eth1.GetGasPrice();
            Console.Out.WriteLine($"Latest Block Number: {ewcLatest}");
            Console.Out.WriteLine($"Gas Price: {ewcCurrentGasPrice} Wei");

            Console.Out.WriteLine("Ethereum Goerli Test Network");
            IN3 goerliClient = IN3.ForChain(Chain.Goerli);
            BigInteger goerliLatest = await goerliClient.Eth1.BlockNumber();
            BigInteger clientCurrentGasPrice = await goerliClient.Eth1.GetGasPrice();
            Console.Out.WriteLine($"Latest Block Number: {goerliLatest}");
            Console.Out.WriteLine($"Gas Price: {clientCurrentGasPrice} Wei");
        }
    }
}