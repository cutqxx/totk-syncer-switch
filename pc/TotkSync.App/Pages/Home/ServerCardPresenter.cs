using TotkSync.App.Services;
using TotkSync.Server;
using Wpf.Ui.Controls;

namespace TotkSync.App.Pages.Home;

public static class ServerCardPresenter
{
    public static ServerCardModel From(ServerState state)
    {
        return state.Phase switch
        {
            ServerPhase.Starting => ServerCardModel.Starting,
            ServerPhase.Running => FromRunning(state),
            ServerPhase.PortBusy or ServerPhase.Failed => Error(state.Message),
            _ => new ServerCardModel { IsBusy = true, Word = "Stopping…" },
        };
    }

    private static ServerCardModel FromRunning(ServerState state)
    {
        return state.Addresses.Count == 0
            ? new ServerCardModel
            {
                Symbol = SymbolRegular.Warning20,
                Tone = StatusTone.Caution,
                Word = "Running",
                Detail = "No home network address found. Connect this PC to the same network as the Switch.",
            }
            : new ServerCardModel
            {
                Symbol = SymbolRegular.CheckmarkCircle20,
                Tone = StatusTone.Success,
                Word = "Running",
                Detail = $"Listening on port {state.Port}",
                Addresses = state.Addresses.Select(address => ServerAddress.For(address, state.Port)).ToList(),
            };
    }

    private static ServerCardModel Error(string detail)
    {
        return new ServerCardModel
        {
            Symbol = SymbolRegular.ErrorCircle20,
            Tone = StatusTone.Critical,
            Word = "Error",
            Detail = detail,
            IsError = true,
        };
    }
}
