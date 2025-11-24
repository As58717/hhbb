package com.example.omnicompose.ui

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.foundation.layout.padding
import androidx.compose.material.icons.Icons
import androidx.compose.material.icons.filled.Chat
import androidx.compose.material.icons.filled.LocationOn
import androidx.compose.material.icons.filled.Person
import androidx.compose.material.icons.filled.Videocam
import androidx.compose.material3.Icon
import androidx.compose.material3.NavigationBar
import androidx.compose.material3.NavigationBarItem
import androidx.compose.material3.Scaffold
import androidx.compose.material3.Text
import androidx.compose.runtime.Composable
import androidx.compose.runtime.getValue
import androidx.compose.ui.Modifier
import androidx.lifecycle.viewmodel.compose.viewModel
import androidx.navigation.NavGraph.Companion.findStartDestination
import androidx.navigation.compose.NavHost
import androidx.navigation.compose.composable
import androidx.navigation.compose.currentBackStackEntryAsState
import androidx.navigation.compose.rememberNavController
import com.example.omnicompose.data.UserRepository

class MainActivity : ComponentActivity() {
    override fun onCreate(savedInstanceState: Bundle?) {
        super.onCreate(savedInstanceState)
        val repository = UserRepository()
        setContent {
            val navController = rememberNavController()
            val vm: MainViewModel = viewModel(factory = MainViewModel.Factory(repository))

            Scaffold(
                bottomBar = { BottomBar(navController) }
            ) { padding ->
                NavHost(
                    navController = navController,
                    startDestination = Screen.Nearby.route,
                    modifier = Modifier.padding(padding)
                ) {
                    composable(Screen.Nearby.route) {
                        NearbyScreen(vm)
                    }
                    composable(Screen.Random.route) {
                        RandomMatchScreen(vm)
                    }
                    composable(Screen.Chat.route) {
                        ChatLobbyScreen(vm)
                    }
                    composable(Screen.Profile.route) {
                        ProfileScreen(vm)
                    }
                }
            }
        }
    }
}

sealed class Screen(val route: String, val title: String, val icon: androidx.compose.ui.graphics.vector.ImageVector) {
    object Nearby : Screen("nearby", "附近", Icons.Default.LocationOn)
    object Random : Screen("random", "速配", Icons.Default.Videocam)
    object Chat : Screen("chat", "聊天", Icons.Default.Chat)
    object Profile : Screen("profile", "我的", Icons.Default.Person)
}

@Composable
fun BottomBar(navController: androidx.navigation.NavHostController) {
    val items = listOf(Screen.Nearby, Screen.Random, Screen.Chat, Screen.Profile)
    val navBackStackEntry by navController.currentBackStackEntryAsState()
    val currentDestination = navBackStackEntry?.destination
    NavigationBar {
        items.forEach { screen ->
            val selected = currentDestination?.route == screen.route
            NavigationBarItem(
                selected = selected,
                onClick = {
                    navController.navigate(screen.route) {
                        popUpTo(navController.graph.findStartDestination().id) {
                            saveState = true
                        }
                        launchSingleTop = true
                        restoreState = true
                    }
                },
                icon = { Icon(screen.icon, contentDescription = screen.title) },
                label = { Text(screen.title) }
            )
        }
    }
}
