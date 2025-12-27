#pragma once

class Client;
namespace lanp2p
{
	class LanP2PNode;
}

bool SeekPeer(Client &client, lanp2p::LanP2PNode &node);
int RunGame(Client* client=nullptr);
