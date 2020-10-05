using System;
using System.Runtime.InteropServices;
using In3.Configuration;

namespace In3.Native
{
    internal static class Configuration
    {
        public static ClientConfiguration Read(IntPtr client)
        {
            IntPtr jsonPointer = in3_get_config(client);
            string jsonConfig = Marshal.PtrToStringUTF8(jsonPointer);
            Utils._free_(jsonPointer);
            return ClientConfiguration.FromJson(jsonConfig);
        }

        internal static string SetConfig(IntPtr client, string val)
        {
            IntPtr jsonPointer = in3_configure(client, val);
            if (jsonPointer != IntPtr.Zero)
            {
                string error = Marshal.PtrToStringUTF8(jsonPointer);
                Utils._free_(jsonPointer);
                return error;
            }

            return null;
        }

        // Why this is an IntPtr and not a string: https://stackoverflow.com/questions/7322503/pinvoke-how-to-free-a-mallocd-string
        [DllImport("libin3", CharSet = CharSet.Ansi)] private static extern IntPtr in3_get_config(IntPtr client);
        [DllImport("libin3", CharSet = CharSet.Ansi)] private static extern IntPtr in3_configure(IntPtr client, string val);
    }
}