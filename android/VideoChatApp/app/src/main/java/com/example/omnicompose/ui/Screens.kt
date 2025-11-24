package com.example.omnicompose.ui

import androidx.compose.foundation.Image
import androidx.compose.foundation.background
import androidx.compose.foundation.clickable
import androidx.compose.foundation.layout.Arrangement
import androidx.compose.foundation.layout.Column
import androidx.compose.foundation.layout.Row
import androidx.compose.foundation.layout.Spacer
import androidx.compose.foundation.layout.fillMaxSize
import androidx.compose.foundation.layout.fillMaxWidth
import androidx.compose.foundation.layout.height
import androidx.compose.foundation.layout.padding
import androidx.compose.foundation.layout.size
import androidx.compose.foundation.lazy.LazyColumn
import androidx.compose.foundation.lazy.items
import androidx.compose.foundation.shape.CircleShape
import androidx.compose.material3.Button
import androidx.compose.material3.Card
import androidx.compose.material3.CardDefaults
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.LaunchedEffect
import androidx.compose.runtime.collectAsState
import androidx.compose.runtime.getValue
import androidx.compose.ui.Alignment
import androidx.compose.ui.Modifier
import androidx.compose.ui.draw.clip
import androidx.compose.ui.graphics.Color
import androidx.compose.ui.layout.ContentScale
import androidx.compose.ui.text.font.FontWeight
import androidx.compose.ui.unit.dp
import coil.compose.rememberAsyncImagePainter
import com.example.omnicompose.data.UserProfile

@Composable
fun NearbyScreen(vm: MainViewModel) {
    val users by vm.users.collectAsState(emptyList())
    LaunchedEffect(Unit) { vm.refreshNearby("杭州") }
    UserList(
        title = "附近同城",
        users = users,
        onClick = { vm.openProfile(it) }
    )
}

@Composable
fun RandomMatchScreen(vm: MainViewModel) {
    val match by vm.randomMatch.collectAsState()
    Column(
        modifier = Modifier.fillMaxSize().padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(16.dp),
        horizontalAlignment = Alignment.CenterHorizontally
    ) {
        Text("速配匹配", style = MaterialTheme.typography.titleLarge)
        match?.let {
            ProfileCard(it)
        }
        Button(onClick = { vm.findRandom() }) {
            Text("开始速配")
        }
    }
}

@Composable
fun ChatLobbyScreen(vm: MainViewModel) {
    val sessions by vm.mockConversations.collectAsState(emptyList())
    UserList(title = "聊天列表", users = sessions.map { it.callee }) { vm.openChat(it) }
}

@Composable
fun ProfileScreen(vm: MainViewModel) {
    val user = vm.users.collectAsState(emptyList()).value.firstOrNull()
    Column(
        modifier = Modifier.fillMaxSize().padding(16.dp),
        verticalArrangement = Arrangement.spacedBy(8.dp)
    ) {
        user?.let { ProfileCard(it) }
        Button(onClick = { vm.startVerification() }) { Text("开启视频验证") }
    }
}

@Composable
private fun UserList(title: String, users: List<UserProfile>, onClick: (UserProfile) -> Unit) {
    Column(modifier = Modifier.fillMaxSize()) {
        Text(title, style = MaterialTheme.typography.titleLarge, modifier = Modifier.padding(16.dp))
        LazyColumn(verticalArrangement = Arrangement.spacedBy(12.dp)) {
            items(users) { user ->
                ProfileRow(user, onClick)
            }
        }
    }
}

@Composable
private fun ProfileRow(user: UserProfile, onClick: (UserProfile) -> Unit) {
    Row(
        modifier = Modifier
            .fillMaxWidth()
            .clickable { onClick(user) }
            .padding(horizontal = 16.dp),
        verticalAlignment = Alignment.CenterVertically
    ) {
        Image(
            painter = rememberAsyncImagePainter(user.avatarUrl),
            contentDescription = null,
            modifier = Modifier.size(64.dp).clip(CircleShape).background(Color.LightGray),
            contentScale = ContentScale.Crop
        )
        Column(modifier = Modifier.padding(start = 12.dp)) {
            Text(user.name, fontWeight = FontWeight.Bold)
            Text("${user.age} · ${user.city}")
            Text(user.bio, maxLines = 1, style = MaterialTheme.typography.bodySmall)
        }
    }
}

@Composable
private fun ProfileCard(user: UserProfile) {
    Card(
        modifier = Modifier.fillMaxWidth(),
        colors = CardDefaults.cardColors(containerColor = MaterialTheme.colorScheme.surfaceVariant)
    ) {
        Column(modifier = Modifier.padding(16.dp), verticalArrangement = Arrangement.spacedBy(8.dp)) {
            Row(verticalAlignment = Alignment.CenterVertically) {
                Image(
                    painter = rememberAsyncImagePainter(user.avatarUrl),
                    contentDescription = null,
                    modifier = Modifier.size(72.dp).clip(CircleShape).background(Color.LightGray),
                    contentScale = ContentScale.Crop
                )
                Column(modifier = Modifier.padding(start = 12.dp)) {
                    Text(user.name, style = MaterialTheme.typography.titleMedium)
                    Text("${user.age}岁 · ${user.city}")
                }
            }
            Text(user.bio)
            Row(horizontalArrangement = Arrangement.spacedBy(8.dp)) {
                user.interests.forEach {
                    Text(text = it, modifier = Modifier.background(Color.DarkGray.copy(alpha = 0.1f)).padding(6.dp))
                }
            }
        }
    }
    Spacer(modifier = Modifier.height(8.dp))
}
